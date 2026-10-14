// SPDX-License-Identifier: GPL-2.0
/*
 * `ntprof record`: attach, collect records, report every TIME_INTERVAL
 * (online) and at the end, and optionally save the records.
 *
 * With --role both, host and target programs share one ring buffer, so a
 * command's target record (emitted when its response leaves) is normally
 * consumed before its host record (emitted at completion) and the two are
 * joined as the host record arrives. A host record that comes first waits
 * up to HOST_WAIT_NS for its target record.
 *
 * The recording runs in a child process. Detaching fentry programs can wait
 * for a busy CPU, uninterruptibly (loader.c); the child prints its report,
 * tells the parent, and only then detaches. The parent holds no BPF object:
 * it returns as soon as the hooks are gone, or after DETACH_WAIT_MS, leaving
 * the child to finish in the background.
 */
#define _GNU_SOURCE /* pipe2 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
/* Bound on unmatched records per (cntlid, qid, cid). */
#define TARGETS_PER_KEY 64
/* How long a host record waits for a target record that comes after it. */
#define HOST_WAIT_NS (100ull * 1000000ull)
/* Say so when no command at all has been seen after this long. */
#define IDLE_HINT_NS (5ull * 1000000000ull)
/* How long the parent waits for the hooks to go once the report is out. */
#define DETACH_WAIT_MS 3000

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

static void add_host(struct session* s, const struct ntp_host_rec* h,
                     const struct ntp_tgt_rec* t) {
  ntp_stats_add(&s->interval, h, t);
  ntp_stats_add(&s->total, h, t);
}

static void add_unmatched(void* ctx, const struct ntp_host_rec* h) {
  add_host(ctx, h, NULL);
}

static int on_record(void* ctx, const void* data, size_t size) {
  struct session* s = ctx;
  const struct ntp_rec_hdr* hdr = data;

  if (hdr->kind == NTP_REC_HOST && size == sizeof(struct ntp_host_rec)) {
    struct ntp_tgt_rec t;

    s->host_records++;
    if (!s->joiner)
      add_host(s, data, NULL);
    else if (ntp_joiner_host_arrives(s->joiner, data, &t))
      add_host(s, data, &t);
  } else if (hdr->kind == NTP_REC_TARGET && size == sizeof(struct ntp_tgt_rec)) {
    struct ntp_host_rec h;

    s->target_records++;
    if (s->joiner && ntp_joiner_target_arrives(s->joiner, data, &h))
      add_host(s, &h, data);
  } else {
    s->malformed++;
    return 0;
  }
  if (s->writing && ntp_writer_add(&s->w, data, size)) s->write_errors++;
  return 0;
}

static void read_tput(const struct ntp_config* c, struct ntp_loader* l,
                      struct ntp_tput* t) {
  t->read_ios = ntp_loader_counter(l, NTP_CNT_HOST_READ_IOS);
  t->read_bytes = ntp_loader_counter(l, NTP_CNT_HOST_READ_BYTES);
  t->write_ios = ntp_loader_counter(l, NTP_CNT_HOST_WRITE_IOS);
  t->write_bytes = ntp_loader_counter(l, NTP_CNT_HOST_WRITE_BYTES);
  /* SPDK reports sampled commands only; its counts are scaled up */
  t->estimated = c->spdk_host[0] != '\0';
}

static void tput_delta(const struct ntp_tput* now, const struct ntp_tput* prev,
                       double seconds, struct ntp_tput* out) {
  out->seconds = seconds;
  out->read_ios = now->read_ios - prev->read_ios;
  out->read_bytes = now->read_bytes - prev->read_bytes;
  out->write_ios = now->write_ios - prev->write_ios;
  out->write_bytes = now->write_bytes - prev->write_bytes;
  out->estimated = now->estimated;
}

/* Everything the counters say, for -v. */
static void print_counters_verbose(struct ntp_loader* l, const struct session* s) {
  const struct ntp_config* c = s->c;

  printf("records: host %llu, target %llu", (unsigned long long)s->host_records,
         (unsigned long long)s->target_records);
  if (c->role & NTP_ROLE_HOST)
    printf(" | host sampled %llu",
           (unsigned long long)ntp_loader_counter(l, NTP_CNT_HOST_SAMPLED));
  if (c->role & NTP_ROLE_TARGET)
    printf(c->spdk_target[0] ? " | target commands ~%llu (estimated), sampled %llu"
                             : " | target commands %llu, sampled %llu",
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

static void print_counters(struct ntp_loader* l, const struct session* s) {
  const struct ntp_config* c = s->c;
  uint64_t drops = ntp_loader_counter(l, NTP_CNT_RB_DROPS);
  uint64_t stale = ntp_loader_counter(l, NTP_CNT_STALE);
  uint64_t sampled = ntp_loader_counter(l, NTP_CNT_HOST_SAMPLED) +
                     ntp_loader_counter(l, NTP_CNT_TGT_SAMPLED);

  if (ntp_verbose) {
    putchar('\n');
    print_counters_verbose(l, s);
    return;
  }
  printf("\nrecords:");
  if (c->role & NTP_ROLE_HOST)
    printf(" %llu initiator", (unsigned long long)s->host_records);
  if (c->role & NTP_ROLE_TARGET)
    printf("%s %llu target", c->role & NTP_ROLE_HOST ? "," : "",
           (unsigned long long)s->target_records);
  if (s->joiner) {
    const struct ntp_join_stats* js = ntp_joiner_stats(s->joiner);

    printf("; %llu matched (%.0f %% of the initiator's)",
           (unsigned long long)js->matched,
           s->host_records ? 100.0 * js->matched / s->host_records : 0.0);
  }
  putchar('\n');
  if (drops)
    printf("warning: %llu records lost, the ring buffer was full: sample fewer "
           "commands (-s)\n",
           (unsigned long long)drops);
  if (stale * 100 > sampled)
    printf("note: %llu records were replaced before their command completed "
           "(aborted commands, or many commands in flight)\n",
           (unsigned long long)stale);
  if (s->malformed)
    printf("warning: %llu malformed records ignored\n",
           (unsigned long long)s->malformed);
}

/* Commands seen so far by the role's hooks (sampled or not). */
static uint64_t commands_seen(const struct ntp_config* c, struct ntp_loader* l) {
  if (c->role & NTP_ROLE_HOST)
    return ntp_loader_counter(l, NTP_CNT_HOST_READ_IOS) +
           ntp_loader_counter(l, NTP_CNT_HOST_WRITE_IOS);
  return ntp_loader_counter(l, NTP_CNT_TGT_CMDS);
}

static const char* role_name(enum ntp_role r) {
  return r == NTP_ROLE_BOTH ? "host+target" : r == NTP_ROLE_HOST ? "host" : "target";
}

/* Close the record file; @loaded: say how many records it got. */
static int close_writer(struct session* s, int ret, bool loaded) {
  const struct ntp_config* c = s->c;

  if (!s->writing) return ret;
  s->writing = false;
  if (ntp_writer_close(&s->w) || s->write_errors) {
    ntp_err("writing %s failed (%llu records lost)", c->output,
            (unsigned long long)s->write_errors);
    return EXIT_FAILURE;
  }
  if (loaded)
    fprintf(stderr, "ntprof: %llu records written to %s\n",
            (unsigned long long)s->w.records, c->output);
  return ret;
}

/* Tell the parent that the results are out, and with which exit status. */
static void report_done(int done_fd, int ret) {
  unsigned char status = (unsigned char)ret;

  fflush(stdout);
  fflush(stderr);
  if (done_fd >= 0 && write(done_fd, &status, 1) != 1) {
    /* the parent is gone: nobody to tell */
  }
}

/* The recording itself; @done_fd: where to say that the report is out. */
static int record(const struct ntp_config* c, int done_fd) {
  struct session s = {.c = c};
  struct ntp_tput start_tput, prev_tput, cur, delta;
  struct ntp_report_opts ro;
  struct ntp_file_header hdr;
  struct ntp_loader* l;
  uint64_t start, prev_tick, next_tick, interval_ns, end;
  bool idle_hint = false, header = false;
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
  if (c->role == NTP_ROLE_TARGET && !c->output[0])
    ntp_warn("a target's records are only reported joined with the "
             "initiator's: save them with -o FILE");

  interval_ns = (uint64_t)c->interval_s * 1000000000ull;
  start = prev_tick = now_mono_ns();
  next_tick = start + interval_ns;
  read_tput(c, l, &start_tput);
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
    if (!idle_hint && now - start >= IDLE_HINT_NS) {
      idle_hint = true;
      if (!commands_seen(c, l))
        ntp_warn("no NVMe-oF command seen in %llu s: is I/O running on an %s "
                 "device%s%s?",
                 IDLE_HINT_NS / 1000000000ull,
                 c->spdk_host[0] || c->spdk_target[0] ? "SPDK"
                 : c->transport == NTP_TR_RDMA         ? "NVMe/RDMA"
                                                       : "NVMe/TCP",
                 strcmp(c->session, "all") ? ", " : "",
                 strcmp(c->session, "all") ? c->session : "");
    }
    if (now < next_tick) continue;
    next_tick += interval_ns;

    if (s.joiner) {
      ntp_joiner_release_hosts(s.joiner, now - HOST_WAIT_NS, add_unmatched, &s);
      ntp_joiner_expire(s.joiner, now - TARGET_EXPIRY_NS);
    }
    if (c->online && (c->role & NTP_ROLE_HOST)) {
      read_tput(c, l, &cur);
      tput_delta(&cur, &prev_tput, (now - prev_tick) / 1e9, &delta);
      if (ro.detail) {
        printf("\n--- interval %.1f-%.1f s ---\n", (prev_tick - start) / 1e9,
               (now - start) / 1e9);
        if (c->rep_throughput) ntp_report_tput(stdout, &delta);
        ntp_report_text(stdout, &s.interval, &ro);
      } else {
        if (!header) {
          printf("every %u s, per group: sampled commands, rate, latency, and "
                 "each stage's share of the mean%s\n",
                 c->interval_s,
                 c->role & NTP_ROLE_TARGET
                     ? ""
                     : "\n(*: S3 to S9, the target's side is not recorded)");
          ntp_report_interval_header(stdout);
          header = true;
        }
        ntp_report_interval(stdout, &s.interval, &ro, (now - start) / 1e9);
      }
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
  read_tput(c, l, &cur);
  ntp_loader_disarm(l);
  for (uint64_t until = now_mono_ns() + DRAIN_NS; now_mono_ns() < until;)
    ntp_loader_poll(l, 10);
  while (ntp_loader_poll(l, 0) > 0) {
  }
  if (s.joiner) ntp_joiner_release_hosts(s.joiner, UINT64_MAX, add_unmatched, &s);
  if (c->role & NTP_ROLE_HOST) {
    tput_delta(&cur, &start_tput, (end - start) / 1e9, &delta);
    printf("\n=== %.1f s, one command in %u sampled ===\n", (end - start) / 1e9,
           c->sample_every);
    if (c->rep_throughput) ntp_report_tput(stdout, &delta);
    ntp_report_text(stdout, &s.total, &ro);
    if (c->json[0] && ntp_report_json(c->json, &s.total, &hdr, &delta))
      ret = EXIT_FAILURE;
  }
  print_counters(l, &s);
  if (c->role == NTP_ROLE_TARGET && c->output[0])
    printf("report: ntprof report HOST.ntp %s (the initiator's recording, "
           "same -s)\n",
           c->output);
  ret = close_writer(&s, ret, true);
  report_done(done_fd, ret);
  ntp_loader_close(l);
  goto out_stats;

out_writer:
  ret = close_writer(&s, ret, false);
out_stats:
  ntp_joiner_free(s.joiner);
  ntp_stats_free(&s.interval);
  ntp_stats_free(&s.total);
  return ret;
}

/* ---- the parent ---- */

static volatile sig_atomic_t child_pid, interrupts;

static void forward_signal(int sig) {
  if (sig == SIGINT) interrupts++;
  if (child_pid > 0) kill(child_pid, sig);
}

static int reap(pid_t pid, int options) {
  int st;
  pid_t r;

  while ((r = waitpid(pid, &st, options)) < 0 && errno == EINTR) {
  }
  if (r == 0) return -1; /* still running */
  if (r < 0) return EXIT_FAILURE;
  return WIFEXITED(st) ? WEXITSTATUS(st) : EXIT_FAILURE;
}

int cmd_record(const struct ntp_config* c) {
  struct sigaction sa = {.sa_handler = forward_signal}; /* no SA_RESTART */
  unsigned char status;
  int fds[2], ret;
  ssize_t n;
  pid_t pid;

  if (pipe2(fds, O_CLOEXEC)) return record(c, -1);
  fflush(NULL);
  pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return record(c, -1);
  }
  if (pid == 0) {
    close(fds[0]);
    exit(record(c, fds[1]));
  }
  close(fds[1]);
  child_pid = pid;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  while ((n = read(fds[0], &status, 1)) < 0 && errno == EINTR) {
    /* a second Ctrl-C: the child is stuck attaching, do not wait for it */
    if (interrupts >= 2) {
      ntp_warn("still attaching in the background (process %d), held up by a "
               "busy CPU; it stops by itself once attached",
               (int)pid);
      return 130;
    }
  }
  close(fds[0]);
  if (n != 1) return reap(pid, 0); /* ended without a report: an error */

  for (int waited = 0; waited < DETACH_WAIT_MS; waited += 50) {
    ret = reap(pid, WNOHANG);
    if (ret >= 0) return ret;
    usleep(50 * 1000);
  }
  ntp_warn("the hooks are still being removed in the background (process %d), "
           "held up by a busy CPU; this ends by itself",
           (int)pid);
  return status;
}

int cmd_probe(const struct ntp_config* c) {
  return ntp_probe(c, stdout) ? EXIT_FAILURE : EXIT_SUCCESS;
}
