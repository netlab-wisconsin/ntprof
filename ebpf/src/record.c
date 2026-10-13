// SPDX-License-Identifier: GPL-2.0
/*
 * `ntprof record`: attach, collect records, report every TIME_INTERVAL
 * (online) and at the end, and optionally save the records.
 *
 * With --role both, host and target programs share one ring buffer, so a
 * command's target record (emitted when its response leaves) is consumed
 * before its host record (emitted at block_rq_complete) and the two are
 * joined as the host record arrives.
 */
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "join.h"
#include "loader.h"
#include "report.h"
#include "stats.h"
#include "trace_file.h"
#include "util.h"

/* Unmatched target records older than this are dropped (one machine only). */
#define TARGET_EXPIRY_NS (10ull * 1000000000ull)
/* Time for commands in flight at the end to complete. */
#define DRAIN_NS (200ull * 1000000ull)
/* Bound on unmatched target records per (cntlid, qid, cid). */
#define TARGETS_PER_KEY 64

static volatile sig_atomic_t stop;

static void on_signal(int sig) { stop = 1; }

struct session {
  const struct ntp_config* c;
  struct ntp_writer w;
  bool writing;
  uint64_t write_errors;
  struct ntp_joiner* joiner; /* role both */
  struct ntp_stats interval, total;
  uint64_t host_records, target_records, malformed;
};

static int on_record(void* ctx, const void* data, size_t size) {
  struct session* s = ctx;
  const struct ntp_rec_hdr* hdr = data;

  if (hdr->kind == NTP_REC_HOST && size == sizeof(struct ntp_host_rec)) {
    const struct ntp_host_rec* h = data;
    struct ntp_tgt_rec t;
    bool m = s->joiner && ntp_joiner_match(s->joiner, h, &t);

    s->host_records++;
    ntp_stats_add(&s->interval, h, m ? &t : NULL);
    ntp_stats_add(&s->total, h, m ? &t : NULL);
  } else if (hdr->kind == NTP_REC_TARGET && size == sizeof(struct ntp_tgt_rec)) {
    s->target_records++;
    if (s->joiner) ntp_joiner_add_target(s->joiner, data);
  } else {
    s->malformed++;
    return 0;
  }
  if (s->writing && ntp_writer_add(&s->w, data, size)) s->write_errors++;
  return 0;
}

static void read_tput(struct ntp_loader* l, struct ntp_tput* t) {
  t->read_ios = ntp_loader_counter(l, NTP_CNT_HOST_READ_IOS);
  t->read_bytes = ntp_loader_counter(l, NTP_CNT_HOST_READ_BYTES);
  t->write_ios = ntp_loader_counter(l, NTP_CNT_HOST_WRITE_IOS);
  t->write_bytes = ntp_loader_counter(l, NTP_CNT_HOST_WRITE_BYTES);
}

static void tput_delta(const struct ntp_tput* now, const struct ntp_tput* prev,
                       double seconds, struct ntp_tput* out) {
  out->seconds = seconds;
  out->read_ios = now->read_ios - prev->read_ios;
  out->read_bytes = now->read_bytes - prev->read_bytes;
  out->write_ios = now->write_ios - prev->write_ios;
  out->write_bytes = now->write_bytes - prev->write_bytes;
}

static void print_counters(struct ntp_loader* l, const struct session* s) {
  const struct ntp_config* c = s->c;

  printf("records: host %llu, target %llu", (unsigned long long)s->host_records,
         (unsigned long long)s->target_records);
  if (c->role & NTP_ROLE_HOST)
    printf(" | host sampled %llu",
           (unsigned long long)ntp_loader_counter(l, NTP_CNT_HOST_SAMPLED));
  if (c->role & NTP_ROLE_TARGET)
    printf(" | target commands %llu, sampled %llu",
           (unsigned long long)ntp_loader_counter(l, NTP_CNT_TGT_CMDS),
           (unsigned long long)ntp_loader_counter(l, NTP_CNT_TGT_SAMPLED));
  printf(" | lost: ring buffer %llu, map full %llu, stale %llu\n",
         (unsigned long long)ntp_loader_counter(l, NTP_CNT_RB_DROPS),
         (unsigned long long)ntp_loader_counter(l, NTP_CNT_MAP_FULL),
         (unsigned long long)ntp_loader_counter(l, NTP_CNT_STALE));
  if (s->joiner) {
    const struct ntp_join_stats* js = ntp_joiner_stats(s->joiner);

    printf("join: %llu matched, %llu host records without target, %zu target "
           "records waiting, %llu dropped\n",
           (unsigned long long)js->matched,
           (unsigned long long)js->host_unmatched, ntp_joiner_pending(s->joiner),
           (unsigned long long)(js->targets_evicted + js->targets_expired));
  }
  if (s->malformed)
    printf("malformed records: %llu\n", (unsigned long long)s->malformed);
}

static const char* role_name(enum ntp_role r) {
  return r == NTP_ROLE_BOTH ? "host+target" : r == NTP_ROLE_HOST ? "host" : "target";
}

int cmd_record(const struct ntp_config* c) {
  struct session s = {.c = c};
  struct ntp_tput start_tput, prev_tput, cur, delta;
  struct ntp_report_opts ro;
  struct ntp_file_header hdr;
  struct ntp_loader* l;
  uint64_t start, prev_tick, next_tick, interval_ns, end;
  int ret = EXIT_SUCCESS;

  ntp_report_opts_from_config(&ro, c);
  ntp_stats_init(&s.interval, c->group_by_type, c->group_by_size,
                 c->group_by_session);
  ntp_stats_init(&s.total, c->group_by_type, c->group_by_size,
                 c->group_by_session);
  s.interval.sample_every = s.total.sample_every = c->sample_every;
  if (c->role == NTP_ROLE_BOTH) s.joiner = ntp_joiner_new(TARGETS_PER_KEY);

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  ntp_header_init(&hdr, c->role, c->transport, c->sample_every);
  if (c->output[0]) {
    if (ntp_writer_open(&s.w, c->output, &hdr)) {
      ret = EXIT_FAILURE;
      goto out_stats;
    }
    s.writing = true;
  }
  l = ntp_loader_open(c, on_record, &s);
  if (!l) {
    ret = EXIT_FAILURE;
    goto out_writer;
  }

  fprintf(stderr,
          "ntprof: profiling as %s, sampling 1 of %u commands%s; Ctrl-C to stop\n",
          role_name(c->role), c->sample_every,
          c->output[0] ? ", saving records" : "");

  interval_ns = (uint64_t)c->interval_s * 1000000000ull;
  start = prev_tick = now_mono_ns();
  next_tick = start + interval_ns;
  read_tput(l, &start_tput);
  prev_tput = start_tput;

  while (!stop) {
    uint64_t now;
    int err = ntp_loader_poll(l, 100);

    if (err < 0) {
      ntp_err("polling the ring buffer failed: %s", strerror(-err));
      ret = EXIT_FAILURE;
      break;
    }
    now = now_mono_ns();
    if (c->duration_s && now - start >= (uint64_t)c->duration_s * 1000000000ull)
      break;
    if (now < next_tick) continue;
    next_tick += interval_ns;

    if (s.joiner) ntp_joiner_expire(s.joiner, now - TARGET_EXPIRY_NS);
    if (c->online && (c->role & NTP_ROLE_HOST)) {
      read_tput(l, &cur);
      tput_delta(&cur, &prev_tput, (now - prev_tick) / 1e9, &delta);
      printf("\n--- interval %.1f-%.1f s ---\n", (prev_tick - start) / 1e9,
             (now - start) / 1e9);
      if (c->rep_throughput) ntp_report_tput(stdout, &delta);
      ntp_report_text(stdout, &s.interval, &ro);
      fflush(stdout);
      prev_tput = cur;
    }
    ntp_stats_reset(&s.interval);
    prev_tick = now;
  }

  /*
   * Stop sampling, give the commands in flight time to complete and emit
   * their records, then drain.
   */
  end = now_mono_ns();
  read_tput(l, &cur);
  ntp_loader_disarm(l);
  for (uint64_t until = now_mono_ns() + DRAIN_NS; now_mono_ns() < until;)
    ntp_loader_poll(l, 10);
  while (ntp_loader_poll(l, 0) > 0) {
  }
  if (c->role & NTP_ROLE_HOST) {
    tput_delta(&cur, &start_tput, (end - start) / 1e9, &delta);
    printf("\n=== total (%.1f s) ===\n", (end - start) / 1e9);
    if (c->rep_throughput) ntp_report_tput(stdout, &delta);
    ntp_report_text(stdout, &s.total, &ro);
    if (c->json[0] && ntp_report_json(c->json, &s.total, &hdr, &delta))
      ret = EXIT_FAILURE;
  }
  print_counters(l, &s);
  ntp_loader_close(l);

out_writer:
  if (s.writing) {
    if (ntp_writer_close(&s.w) || s.write_errors) {
      ntp_err("writing %s failed (%llu records lost)", c->output,
              (unsigned long long)s.write_errors);
      ret = EXIT_FAILURE;
    } else if (l) {
      fprintf(stderr, "ntprof: %llu records written to %s\n",
              (unsigned long long)s.w.records, c->output);
    }
  }
out_stats:
  ntp_joiner_free(s.joiner);
  ntp_stats_free(&s.interval);
  ntp_stats_free(&s.total);
  return ret;
}

int cmd_probe(const struct ntp_config* c) {
  return ntp_probe(c, stdout) ? EXIT_FAILURE : EXIT_SUCCESS;
}
