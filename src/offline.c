// SPDX-License-Identifier: GPL-2.0
/* `ntprof report` and `ntprof compare`: analysis of record files. */
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "join.h"
#include "report.h"
#include "stats.h"
#include "trace_file.h"
#include "util.h"

struct run {
  struct ntp_joiner* joiner;
  struct ntp_host_rec* hosts;
  size_t nhosts, cap;
  uint64_t targets;
  struct ntp_file_header host_hdr;
  bool have_host_hdr;
};

static int on_host(const struct ntp_host_rec* h, void* arg) {
  struct run* r = arg;

  if (r->nhosts == r->cap) {
    r->cap = r->cap ? r->cap * 2 : 4096;
    r->hosts = xrealloc(r->hosts, r->cap * sizeof(*r->hosts));
  }
  r->hosts[r->nhosts++] = *h;
  return 0;
}

static int on_target(const struct ntp_tgt_rec* t, void* arg) {
  struct run* r = arg;

  ntp_joiner_add_target(r->joiner, t);
  r->targets++;
  return 0;
}

/*
 * Read every file of a run, then join. All target records are read first, so
 * the order of the files does not matter.
 */
static int load_run(struct run* r, char* const* files, int n,
                    struct ntp_stats* stats) {
  static const struct ntp_reader_ops ops = {on_host, on_target};

  memset(r, 0, sizeof(*r));
  r->joiner = ntp_joiner_new(0);
  for (int i = 0; i < n; i++) {
    struct ntp_file_header hdr;

    if (ntp_read_file(files[i], &hdr, &ops, r)) return -1;
    if ((hdr.role & NTP_ROLE_HOST) && !r->have_host_hdr) {
      r->host_hdr = hdr;
      r->have_host_hdr = true;
    }
  }
  for (size_t i = 0; i < r->nhosts; i++) {
    struct ntp_tgt_rec t;
    bool m = ntp_joiner_match(r->joiner, &r->hosts[i], &t);

    ntp_stats_add(stats, &r->hosts[i], m ? &t : NULL);
  }
  return 0;
}

static void free_run(struct run* r) {
  ntp_joiner_free(r->joiner);
  free(r->hosts);
}

static void print_join(const struct run* r) {
  const struct ntp_join_stats* js = ntp_joiner_stats(r->joiner);

  printf("\nrecords: %zu host", r->nhosts);
  if (r->targets)
    printf(", %llu target; %llu matched (%.0f %% of the host's)",
           (unsigned long long)r->targets, (unsigned long long)js->matched,
           r->nhosts ? 100.0 * js->matched / r->nhosts : 0.0);
  putchar('\n');
  if (r->targets && js->host_unmatched * 100 > r->nhosts)
    printf("  %llu host records found no target record: start the target's "
           "recording first and stop it last, with the same -s\n",
           (unsigned long long)js->host_unmatched);
}

int cmd_report(const struct ntp_config* c, char* const* files, int nfiles) {
  struct ntp_report_opts ro;
  struct ntp_stats stats;
  struct run r;
  int ret = EXIT_SUCCESS;

  if (nfiles < 1) {
    ntp_err("report: give a host record file, and optionally the target's");
    return EXIT_FAILURE;
  }
  ntp_report_opts_from_config(&ro, c);
  ntp_stats_init(&stats, c->group_by_type, c->group_by_size, c->group_by_session);
  if (load_run(&r, files, nfiles, &stats)) {
    ret = EXIT_FAILURE;
    goto out;
  }
  if (!r.have_host_hdr) {
    ntp_err("report: none of the files is a host recording; "
            "target records are reported with the host's");
    ret = EXIT_FAILURE;
    goto out;
  }
  stats.sample_every = r.host_hdr.sample_every;
  printf("recorded on %s (kernel %s), one command in %u sampled\n",
         r.host_hdr.hostname, r.host_hdr.kernel, r.host_hdr.sample_every);
  ntp_report_text(stdout, &stats, &ro);
  print_join(&r);
  if (c->json[0] && ntp_report_json(c->json, &stats, &r.host_hdr, NULL))
    ret = EXIT_FAILURE;
out:
  free_run(&r);
  ntp_stats_free(&stats);
  return ret;
}

/* ---- dump ---- */

static const char* const host_ts_names[] = {
    "start",      "queue_rq",  "cmd_sent", "r2t_arr",  "r2t_proc",
    "data_start", "data_sent", "c2h_arr",  "c2h_proc", "rsp_arr",
    "rsp_proc",   "nvme_done", "blk_done", "send_done", "c2h_last_arr",
    "c2h_done",
};

static const char* const tgt_ts_names[] = {
    "cmd_arr",  "cmd_proc", "r2t_queued", "r2t_sent", "data_arr",
    "data_proc", "exec",    "be_issue",   "be_done",  "complete",
    "rsp_queued", "c2h_sent", "rsp_sent", "send_done", "data_last_arr",
    "data_done",
};

/* an event added to bpf/ntprof.h needs its name here too */
_Static_assert(ARRAY_SIZE(host_ts_names) == NTP_H_NR, "a name for every host event");
_Static_assert(ARRAY_SIZE(tgt_ts_names) == NTP_T_NR, "a name for every target event");

static void dump_id(const struct ntp_rec_hdr* h, const struct ntp_cmd_id* id) {
  printf("%u,%#x,%u,%u,%u,%u,%u,%u,%llu,%u", h->transport, h->flags, h->cpu,
         id->cntlid, id->qid, id->cid, id->opcode, id->nsid,
         (unsigned long long)id->slba, id->nlb);
}

/* Timestamps relative to the first non-zero one; empty when not seen. */
static void dump_ts(const __u64* ts, int n) {
  __u64 base = 0;

  for (int i = 0; i < n; i++)
    if (ts[i] && (!base || ts[i] < base)) base = ts[i];
  printf(",%llu", (unsigned long long)base);
  for (int i = 0; i < n; i++) {
    if (ts[i])
      printf(",%lld", (long long)(ts[i] - base));
    else
      printf(",");
  }
}

static int dump_host(const struct ntp_host_rec* h, void* arg) {
  printf("host,");
  dump_id(&h->hdr, &h->id);
  printf(",%u,%s,%u,%u,%u", h->bytes, h->disk, h->ctrl_instance, h->q_unsent,
         h->q_inflight);
  dump_ts(h->ts, NTP_H_NR);
  printf(",%llu,%u", (unsigned long long)h->c2h_busy_ns, h->c2h_skbs);
  putchar('\n');
  return 0;
}

static int dump_target(const struct ntp_tgt_rec* t, void* arg) {
  printf("target,");
  dump_id(&t->hdr, &t->id);
  printf(",%u,,,%u,%u", t->bytes, t->q_outstanding, t->q_resp_queue);
  dump_ts(t->ts, NTP_T_NR);
  printf(",%llu,%u", (unsigned long long)t->h2c_busy_ns, t->h2c_skbs);
  putchar('\n');
  return 0;
}

int cmd_dump(char* const* files, int nfiles) {
  static const struct ntp_reader_ops ops = {dump_host, dump_target};

  if (nfiles < 1) {
    ntp_err("dump: give one or more record files");
    return EXIT_FAILURE;
  }
  /* host rows use the host_* names, target rows the target_* names */
  printf("kind,transport,flags,cpu,cntlid,qid,cid,opcode,nsid,slba,nlb,bytes,"
         "disk,ctrl,queue1,queue2,base_ns");
  for (int i = 0; i < NTP_H_NR; i++) printf(",h_%s", host_ts_names[i]);
  printf(",h_c2h_busy_ns,h_c2h_skbs  (target rows: base_ns");
  for (int i = 0; i < NTP_T_NR; i++) printf(",t_%s", tgt_ts_names[i]);
  printf(",t_h2c_busy_ns,t_h2c_skbs)\n");
  for (int i = 0; i < nfiles; i++) {
    struct ntp_file_header hdr;

    if (ntp_read_file(files[i], &hdr, &ops, NULL)) return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

/* Split "a.ntp,b.ntp" into at most two paths (modifies @list). */
static int split_run(char* list, char** files) {
  int n = 0;

  for (char *save = NULL, *tok = strtok_r(list, ",", &save); tok && n < 2;
       tok = strtok_r(NULL, ",", &save))
    files[n++] = tok;
  return n;
}

int cmd_compare(const struct ntp_config* c, const char* base,
                const char* loaded) {
  char* bl = xstrdup(base);
  char* ll = xstrdup(loaded);
  char *bf[2], *lf[2];
  int nb = split_run(bl, bf), nl = split_run(ll, lf);
  struct ntp_report_opts ro;
  struct ntp_stats sb, sl;
  struct run rb, rl;
  int ret = EXIT_SUCCESS;

  ntp_report_opts_from_config(&ro, c);

  ntp_stats_init(&sb, c->group_by_type, c->group_by_size, c->group_by_session);
  ntp_stats_init(&sl, c->group_by_type, c->group_by_size, c->group_by_session);
  memset(&rb, 0, sizeof(rb));
  memset(&rl, 0, sizeof(rl));
  if (!nb || !nl || load_run(&rb, bf, nb, &sb) || load_run(&rl, lf, nl, &sl)) {
    ret = EXIT_FAILURE;
  } else {
    if (rb.have_host_hdr) sb.sample_every = rb.host_hdr.sample_every;
    if (rl.have_host_hdr) sl.sample_every = rl.host_hdr.sample_every;
    printf("LAD (latency amplification degree) = mean under load / mean of the "
           "baseline;\nn/a where the baseline is below 0.5 us\n");
    ntp_report_compare(stdout, &sb, &sl, &ro);
  }
  free_run(&rb);
  free_run(&rl);
  ntp_stats_free(&sb);
  ntp_stats_free(&sl);
  free(bl);
  free(ll);
  return ret;
}
