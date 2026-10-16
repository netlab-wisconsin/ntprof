// SPDX-License-Identifier: GPL-2.0
#include "report.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Component table (--detail): path order, details after what they split. */
static const struct {
  enum ntp_comp comp;
  int indent;
} display[] = {
    {NTP_C_BLK_SUB, 0},      {NTP_C_TR_SUB, 0},      {NTP_C_NET, 0},
    {NTP_C_NET_DATA, 2},     {NTP_C_TGT_RX, 0},       {NTP_C_TGT_SUB, 0},     {NTP_C_TGT_BE, 0},
    {NTP_C_TGT_BE_SUB, 2},   {NTP_C_TGT_DEV, 2},     {NTP_C_TGT_BE_CMPL, 2},
    {NTP_C_TGT_CMPL, 0},     {NTP_C_REMOTE, 0},      {NTP_C_HOST_RX, 0},
    {NTP_C_HOST_WAIT, 0},    {NTP_C_HOST_DATA, 0},
    {NTP_C_TR_CMPL, 0},      {NTP_C_BLK_CMPL, 0},    {NTP_C_CMPL_IPI, 2},
    {NTP_C_E2E, 0},
};

/* A stage mean below this is too small for a ratio to mean anything. */
#define LAD_MIN_BASE_NS 500.0
/* Groups with fewer records than this share are folded into one line. */
#define MINOR_GROUP_PCT 1
/* Width of a share bar at 100 %. */
#define BAR_WIDTH 24

/* quantile indexes in ntp_quantiles */
#define Q_P50 1
#define Q_P99 6
#define Q_P999 7

void ntp_report_opts_from_config(struct ntp_report_opts* o,
                                 const struct ntp_config* c) {
  o->aggr = c->aggr;
  o->distribution = c->rep_distribution;
  o->breakdown = c->rep_breakdown;
  o->detail = c->rep_detail;
  o->queue_length = c->rep_queue_length;
  o->all_groups = c->rep_all_groups;
}

static double us(double ns) { return ns / 1e3; }

/* 950 -> "950", 38712 -> "38.7k", 2.5e6 -> "2.50M" */
static const char* human(double v, char* buf, size_t len) {
  if (v < 1e3)
    snprintf(buf, len, "%.0f", v);
  else if (v < 1e6)
    snprintf(buf, len, "%.1fk", v / 1e3);
  else
    snprintf(buf, len, "%.2fM", v / 1e6);
  return buf;
}

void ntp_report_tput(FILE* f, const struct ntp_tput* t) {
  double s = t->seconds > 0 ? t->seconds : 1;
  bool any = false;

  fprintf(f, "throughput (%s):",
          t->estimated ? "estimated from the sampled commands" : "all commands");
  if (t->read_ios || !t->write_ios) {
    fprintf(f, " read %.1f kIOPS, %.1f MB/s", t->read_ios / s / 1e3,
            t->read_bytes / s / 1e6);
    any = true;
  }
  if (t->write_ios)
    fprintf(f, "%s write %.1f kIOPS, %.1f MB/s", any ? " |" : "",
            t->write_ios / s / 1e3, t->write_bytes / s / 1e6);
  fputc('\n', f);
}

/* ---- groups ---- */

static uint64_t total_records(const struct ntp_stats* s) {
  uint64_t n = 0;

  for (size_t i = 0; i < s->n; i++) n += s->groups[i].records;
  return n;
}

/* Group indexes, the most records first (insertion sort: few groups). */
static size_t* order_groups(const struct ntp_stats* s) {
  size_t* idx = xcalloc(s->n ? s->n : 1, sizeof(*idx));

  for (size_t i = 0; i < s->n; i++) {
    size_t j = i;

    while (j && s->groups[idx[j - 1]].records < s->groups[i].records) {
      idx[j] = idx[j - 1];
      j--;
    }
    idx[j] = i;
  }
  return idx;
}

/* Is the @i-th group in display order folded into the "more groups" line? */
static bool folded(const struct ntp_stats* s, const size_t* idx, size_t i,
                   uint64_t total, const struct ntp_report_opts* o) {
  return i && !o->all_groups &&
         s->groups[idx[i]].records * 100 < total * MINOR_GROUP_PCT;
}

/* "+ 2 more groups with 3 records ..." for the groups not shown. */
static void print_minor(FILE* f, const struct ntp_stats* s, const size_t* idx,
                        uint64_t total, const struct ntp_report_opts* o) {
  uint64_t records = 0;
  size_t n = 0, listed = 0;

  for (size_t i = 0; i < s->n; i++)
    if (folded(s, idx, i, total, o)) {
      records += s->groups[idx[i]].records;
      n++;
    }
  if (!n) return;
  fprintf(f, "+ %zu more group%s with %llu record%s in all (under %d %% each):",
          n, n > 1 ? "s" : "", (unsigned long long)records, records > 1 ? "s" : "",
          MINOR_GROUP_PCT);
  for (size_t i = 0; i < s->n && listed < 4; i++) {
    char label[96];

    if (!folded(s, idx, i, total, o)) continue;
    ntp_group_label(&s->groups[idx[i]].key, label, sizeof(label));
    fprintf(f, "%s %s", listed ? "," : "", label);
    listed++;
  }
  fprintf(f, "%s; --all-groups shows them\n", n > listed ? ", ..." : "");
}

/* ---- the stages of a group ---- */

/*
 * The rows of the stage summary. With target records they are the five
 * stages; without, the target's side is one row. Either way they add up to
 * the end-to-end latency.
 */
struct srow {
  const char* key; /* which stage: compared between runs */
  char desc[48];   /* what users read */
  struct i64vec* v;
  struct ntp_summary m;
};

/* Width of a stage name in the stage, compare and component tables. */
#define NAME_W 38

static bool group_joined(const struct ntp_group* g) {
  return g->stage[NTP_S3_S5].n && 2 * g->stage[NTP_S3_S5].n >= g->comp[NTP_C_E2E].n;
}

#define MAX_SROWS 6

static int stage_rows(struct ntp_group* g, bool joined, struct srow* rows) {
  bool rdma = g->transport == NTP_TR_RDMA;
  const char* host_tr = g->spdk_host ? "SPDK" : rdma ? "nvme-rdma" : "nvme-tcp";
  const char* tgt_tr = g->spdk_target ? "SPDK" : rdma ? "nvmet-rdma" : "nvmet-tcp";
  int n = 0;

  /* SPDK has no block layer: the host block layer is empty */
  if (!g->spdk_host) {
    rows[n].key = ntp_stage_keys[NTP_S1];
    snprintf(rows[n].desc, sizeof(rows[n].desc), "host block layer");
    rows[n++].v = &g->stage[NTP_S1];
  }
  rows[n].key = ntp_stage_keys[NTP_S2];
  snprintf(rows[n].desc, sizeof(rows[n].desc), "host driver (%s)", host_tr);
  rows[n++].v = &g->stage[NTP_S2];

  if (joined) {
    rows[n].key = ntp_stage_keys[NTP_S3_S5];
    snprintf(rows[n].desc, sizeof(rows[n].desc), "%s",
             rdma ? "network (RDMA, NICs, wire)" : "network (TCP/IP, NICs, wire)");
    rows[n++].v = &g->stage[NTP_S3_S5];
    rows[n].key = ntp_stage_keys[NTP_S6];
    snprintf(rows[n].desc, sizeof(rows[n].desc), "target driver (%s)", tgt_tr);
    rows[n++].v = &g->stage[NTP_S6];
    rows[n].key = ntp_stage_keys[NTP_S7_S9];
    snprintf(rows[n].desc, sizeof(rows[n].desc), "%s",
             g->spdk_target ? "storage (SPDK bdev, drive)"
                            : "storage (target block layer, drive)");
    rows[n++].v = &g->stage[NTP_S7_S9];
  } else {
    rows[n].key = "host_receive";
    snprintf(rows[n].desc, sizeof(rows[n].desc), "host receive path");
    rows[n++].v = &g->comp[NTP_C_HOST_RX];
    /* a TCP read's wait for the rest of its data: network, seen from the host */
    if (g->comp[NTP_C_HOST_WAIT].n) {
      rows[n].key = "host_data_wait";
      snprintf(rows[n].desc, sizeof(rows[n].desc), "waiting for the rest of the data");
      rows[n++].v = &g->comp[NTP_C_HOST_WAIT];
    }
    rows[n].key = "network_and_target";
    snprintf(rows[n].desc, sizeof(rows[n].desc),
             "network and target (not recorded)");
    rows[n++].v = &g->comp[NTP_C_REMOTE];
  }
  for (int i = 0; i < n; i++) ntp_summarize(rows[i].v, &rows[i].m);
  return n;
}

static double share(double part, double whole) {
  return whole > 0 ? 100.0 * part / whole : 0;
}

static void bar(FILE* f, double pct) {
  int w = (int)(pct * BAR_WIDTH / 100 + 0.5);

  if (w > BAR_WIDTH) w = BAR_WIDTH;
  for (int i = 0; i < w; i++) fputc('#', f);
}

static void print_latency(FILE* f, const struct ntp_summary* e,
                          const struct ntp_report_opts* o) {
  fprintf(f, "  latency (us): mean %.2f", us(e->mean));
  if (o->aggr == NTP_AGG_MIN) fprintf(f, ", min %.2f", us((double)e->min));
  if (o->distribution) {
    if (o->aggr == NTP_AGG_DIST) {
      for (int i = 0; i < NTP_NQ; i++)
        fprintf(f, ", %s %.2f", ntp_quantile_names[i], us((double)e->q[i]));
    } else {
      fprintf(f, ", p50 %.2f, p99 %.2f, p99.9 %.2f", us((double)e->q[Q_P50]),
              us((double)e->q[Q_P99]), us((double)e->q[Q_P999]));
    }
  }
  if (o->aggr == NTP_AGG_MAX || o->aggr == NTP_AGG_DIST)
    fprintf(f, ", max %.2f", us((double)e->max));
  fputc('\n', f);
}

static void print_stages(FILE* f, struct ntp_group* g, double rate,
                         const struct ntp_summary* e2e,
                         const struct ntp_report_opts* o) {
  struct srow rows[MAX_SROWS];
  int n = stage_rows(g, group_joined(g), rows), top = -1;
  bool inflight = o->queue_length && rate > 0;

  fprintf(f, "  %-*s %8s %6s", NAME_W, "stage", "mean us", "share");
  if (inflight) fprintf(f, " %8s", "avg QD");
  fputc('\n', f);
  for (int i = 0; i < n; i++) {
    double pct = share(rows[i].m.mean, e2e->mean);

    if (!rows[i].m.n) continue;
    fprintf(f, "  %-*s %8.2f %5.0f%%", NAME_W, rows[i].desc, us(rows[i].m.mean), pct);
    if (inflight) fprintf(f, " %8.2f", rate * rows[i].m.mean / 1e9);
    fputs("  ", f);
    bar(f, pct);
    fputc('\n', f);
    if (top < 0 || rows[i].m.mean > rows[top].m.mean) top = i;
  }
  if (top >= 0 && n > 1)
    fprintf(f, "  largest: %s, %.0f %% of the latency\n", rows[top].desc,
            share(rows[top].m.mean, e2e->mean));
}

static void print_queues(FILE* f, struct ntp_group* g) {
  struct ntp_summary m;
  bool any = false;

  for (int q = 0; q < NTP_Q_NR; q++) {
    ntp_summarize(&g->queue[q], &m);
    if (!m.n) continue;
    fprintf(f, "%s %s %.2f/%lld",
            any ? "," : "  queue length at arrival (mean/p99):",
            ntp_queue_names[q], m.mean, (long long)m.q[Q_P99]);
    any = true;
  }
  if (any) fputc('\n', f);
}

/* ---- the component table (--detail) ---- */

/* Which quantile columns to print. */
static int columns(const struct ntp_report_opts* o, int* idx) {
  int n = 0;

  if (!o->distribution) return 0;
  if (o->aggr == NTP_AGG_DIST) {
    for (int i = 0; i < NTP_NQ; i++) idx[n++] = i;
  } else {
    idx[n++] = Q_P50;
    idx[n++] = Q_P99;
  }
  return n;
}

static void print_header(FILE* f, const struct ntp_report_opts* o) {
  int q[NTP_NQ], nq = columns(o, q);

  fprintf(f, "  %-*s %8s %9s %7s", NAME_W, "component", "n", "mean", "L");
  if (o->aggr == NTP_AGG_MIN) fprintf(f, " %9s", "min");
  for (int i = 0; i < nq; i++) fprintf(f, " %9s", ntp_quantile_names[q[i]]);
  if (o->aggr == NTP_AGG_MAX) fprintf(f, " %9s", "max");
  fprintf(f, "   (us)\n");
}

/* @rate: commands per second, for L = rate x mean (Little's law); 0: none */
static void print_row(FILE* f, const char* label, int indent,
                      const struct ntp_summary* m, double rate,
                      const struct ntp_report_opts* o) {
  int q[NTP_NQ], nq = columns(o, q);

  fprintf(f, "  %*s%-*s %8zu %9.2f", indent, "", NAME_W - indent, label, m->n,
          us(m->mean));
  if (rate > 0)
    fprintf(f, " %7.2f", rate * m->mean / 1e9);
  else
    fprintf(f, " %7s", "-");
  if (o->aggr == NTP_AGG_MIN) fprintf(f, " %9.2f", us((double)m->min));
  for (int i = 0; i < nq; i++) fprintf(f, " %9.2f", us((double)m->q[q[i]]));
  if (o->aggr == NTP_AGG_MAX) fprintf(f, " %9.2f", us((double)m->max));
  fputc('\n', f);
}

static void print_components(FILE* f, struct ntp_group* g, double rate,
                             const struct ntp_report_opts* o) {
  struct ntp_summary m;

  fprintf(f, "  components, in path order (L: commands in each, by Little's law)\n");
  print_header(f, o);
  for (size_t i = 0; i < ARRAY_SIZE(display); i++) {
    enum ntp_comp c = display[i].comp;

    ntp_summarize(&g->comp[c], &m);
    if (!m.n) continue;
    print_row(f, ntp_comps[c].desc, display[i].indent, &m, rate, o);
  }
}

/* ---- a group ---- */

static void print_group(FILE* f, const struct ntp_stats* s, struct ntp_group* g,
                        const struct ntp_report_opts* o) {
  double rate = ntp_group_rate(s, g);
  uint64_t usable = g->records - g->bad_host;
  struct ntp_summary e2e;
  char label[96], num[16];

  ntp_group_label(&g->key, label, sizeof(label));
  fprintf(f, "\n== %s: %llu commands sampled", label,
          (unsigned long long)g->records);
  if (rate > 0)
    fprintf(f, " (from about %s commands/s)", human(rate, num, sizeof(num)));
  if (g->joined == usable && usable)
    fprintf(f, ", all with the target's side");
  else if (g->joined)
    fprintf(f, ", %.0f %% with the target's side",
            share((double)g->joined, (double)usable));
  else
    fprintf(f, ", host side only");
  fputc('\n', f);
  if (g->bad_host || g->bad_target)
    fprintf(f, "  incomplete records left out: %llu host, %llu target "
               "(events missed; see ntprof probe)\n",
            (unsigned long long)g->bad_host, (unsigned long long)g->bad_target);
  if (g->post_estimated)
    fprintf(f, "  %llu target records lack the response's posting (a kprobe that "
               "did not run): nvmet_req_complete's time stands in, so their target "
               "driver completion reads 0\n",
            (unsigned long long)g->post_estimated);

  ntp_summarize(&g->comp[NTP_C_E2E], &e2e);
  if (!e2e.n) return;
  print_latency(f, &e2e, o);
  if (o->breakdown) print_stages(f, g, rate, &e2e, o);
  if (o->queue_length) print_queues(f, g);
  if (o->detail) print_components(f, g, rate, o);
}

void ntp_report_text(FILE* f, struct ntp_stats* s,
                     const struct ntp_report_opts* o) {
  uint64_t total = total_records(s);
  size_t* idx;

  if (!s->n) {
    fprintf(f, "no NVMe-oF read or write was sampled: was I/O running on the "
               "profiled device? (one command in -s N is sampled)\n");
    return;
  }
  idx = order_groups(s);
  for (size_t i = 0; i < s->n; i++) {
    struct ntp_group* g = &s->groups[idx[i]];

    if (folded(s, idx, i, total, o)) continue;
    print_group(f, s, g, o);
  }
  print_minor(f, s, idx, total, o);
  free(idx);
}

/* ---- online intervals ---- */

/* Two header lines: the machine over each share column, then the stage. */
void ntp_report_interval_header(FILE* f) {
  fprintf(f, "%8s  %-22s %6s %8s %9s %9s %-15s %7s %-15s\n", "", "", "", "",
          "", "", "     host", "", "    target");
  fprintf(f, "%8s  %-22s %6s %8s %9s %9s %7s %7s %7s %7s %7s\n", "time", "group",
          "n", "cmds/s", "mean us", "p99 us", "block", "driver", "network",
          "driver", "storage");
}

void ntp_report_interval(FILE* f, struct ntp_stats* s,
                         const struct ntp_report_opts* o, double t_s) {
  uint64_t total = total_records(s);
  size_t* idx;

  if (o->detail) {
    fprintf(f, "\n--- %.1f s ---\n", t_s);
    ntp_report_text(f, s, o);
    return;
  }
  if (!s->n) {
    fprintf(f, "%7.1fs  %-22s\n", t_s, "(no sampled commands)");
    return;
  }
  idx = order_groups(s);
  for (size_t i = 0; i < s->n; i++) {
    struct ntp_group* g = &s->groups[idx[i]];
    double rate = ntp_group_rate(s, g);
    struct ntp_summary e2e, m;
    char label[96], num[16];
    bool joined = group_joined(g);

    if (folded(s, idx, i, total, o)) continue;
    ntp_summarize(&g->comp[NTP_C_E2E], &e2e);
    if (!e2e.n) continue;
    ntp_group_label(&g->key, label, sizeof(label));
    fprintf(f, "%7.1fs  %-22.22s %6llu %8s %9.2f %9.2f", t_s, label,
            (unsigned long long)g->records,
            rate > 0 ? human(rate, num, sizeof(num)) : "-",
            us(e2e.mean), us((double)e2e.q[Q_P99]));
    for (int st = 0; st < NTP_STAGE_NR; st++) {
      ntp_summarize(&g->stage[st], &m);
      if (!joined && st == NTP_S3_S5) {
        /* no target records: the rest of the round trip in one column */
        struct ntp_summary rx, rem, wait;

        ntp_summarize(&g->comp[NTP_C_HOST_RX], &rx);
        ntp_summarize(&g->comp[NTP_C_REMOTE], &rem);
        ntp_summarize(&g->comp[NTP_C_HOST_WAIT], &wait);
        fprintf(f, " %5.0f%%*", share(rx.mean + rem.mean + wait.mean, e2e.mean));
        break;
      }
      if (m.n)
        fprintf(f, " %6.0f%%", share(m.mean, e2e.mean));
      else
        fprintf(f, " %7s", "-");
    }
    fputc('\n', f);
  }
  free(idx);
}

/* ---- JSON ---- */

static void json_str(FILE* f, const char* s) {
  fputc('"', f);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;

    if (c == '"' || c == '\\')
      fprintf(f, "\\%c", c);
    else if (c < 0x20)
      fprintf(f, "\\u%04x", c);
    else
      fputc(c, f);
  }
  fputc('"', f);
}

static void json_summary(FILE* f, const struct ntp_summary* m) {
  fprintf(f, "{\"n\": %zu, \"mean_ns\": %.1f, \"min_ns\": %lld, \"max_ns\": %lld",
          m->n, m->mean, (long long)m->min, (long long)m->max);
  for (int i = 0; i < NTP_NQ; i++)
    fprintf(f, ", \"%s_ns\": %lld", ntp_quantile_names[i], (long long)m->q[i]);
  fputc('}', f);
}

int ntp_report_json(const char* path, struct ntp_stats* s,
                    const struct ntp_file_header* meta,
                    const struct ntp_tput* tput) {
  FILE* f = fopen(path, "w");
  struct ntp_summary m;

  if (!f) {
    ntp_err("cannot create %s: %s", path, strerror(errno));
    return -1;
  }
  fprintf(f, "{\n  \"tool\": \"ntprof\",\n");
  if (meta) {
    fprintf(f, "  \"host\": ");
    json_str(f, meta->hostname);
    fprintf(f, ",\n  \"kernel\": ");
    json_str(f, meta->kernel);
    fprintf(f, ",\n  \"sample_every\": %u,\n  \"start_realtime_ns\": %llu,\n",
            meta->sample_every, (unsigned long long)meta->start_real_ns);
  }
  if (tput)
    fprintf(f,
            "  \"throughput\": {\"seconds\": %.3f, \"read_ios\": %llu, "
            "\"read_bytes\": %llu, \"write_ios\": %llu, \"write_bytes\": %llu, "
            "\"estimated\": %s},\n",
            tput->seconds, (unsigned long long)tput->read_ios,
            (unsigned long long)tput->read_bytes,
            (unsigned long long)tput->write_ios,
            (unsigned long long)tput->write_bytes,
            tput->estimated ? "true" : "false");
  fprintf(f, "  \"groups\": [");
  for (size_t gi = 0; gi < s->n; gi++) {
    struct ntp_group* g = &s->groups[gi];
    double rate = ntp_group_rate(s, g);
    bool first = true;

    fprintf(f, "%s\n    {\"type\": ", gi ? "," : "");
    json_str(f, g->key.is_write < 0 ? "any" : g->key.is_write ? "write" : "read");
    fprintf(f, ", \"bytes\": %u, \"session\": ", g->key.bytes);
    json_str(f, g->key.session);
    fprintf(f,
            ", \"records\": %llu, \"joined\": %llu, \"incomplete_host\": %llu, "
            "\"incomplete_target\": %llu, \"post_estimated\": %llu, "
            "\"rate_per_s\": %.1f,\n"
            "     \"components\": {",
            (unsigned long long)g->records, (unsigned long long)g->joined,
            (unsigned long long)g->bad_host, (unsigned long long)g->bad_target,
            (unsigned long long)g->post_estimated, rate);
    for (int c = 0; c < NTP_C_NR; c++) {
      ntp_summarize(&g->comp[c], &m);
      if (!m.n) continue;
      fprintf(f, "%s\n       \"%s\": {\"stage\": \"%s\", \"description\": ",
              first ? "" : ",", ntp_comps[c].name, ntp_comps[c].stage);
      json_str(f, ntp_comps[c].desc);
      fprintf(f, ", \"occupancy\": %.3f, \"summary\": ", rate * m.mean / 1e9);
      json_summary(f, &m);
      fputc('}', f);
      first = false;
    }
    fprintf(f, "},\n     \"stages\": {");
    first = true;
    for (int st = 0; st < NTP_STAGE_NR; st++) {
      ntp_summarize(&g->stage[st], &m);
      if (!m.n) continue;
      fprintf(f, "%s\n       \"%s\": ", first ? "" : ",", ntp_stage_keys[st]);
      json_summary(f, &m);
      first = false;
    }
    fprintf(f, "},\n     \"queues\": {");
    first = true;
    for (int q = 0; q < NTP_Q_NR; q++) {
      ntp_summarize(&g->queue[q], &m);
      if (!m.n) continue;
      fprintf(f, "%s\n       \"%s\": ", first ? "" : ",", ntp_queue_names[q]);
      json_summary(f, &m);
      first = false;
    }
    fprintf(f, "}}");
  }
  fprintf(f, "\n  ]\n}\n");
  if (fclose(f)) {
    ntp_err("cannot write %s", path);
    return -1;
  }
  return 0;
}

/* ---- LAD ---- */

static struct ntp_group* find_group(struct ntp_stats* s,
                                    const struct ntp_group_key* k) {
  for (size_t i = 0; i < s->n; i++)
    if (!memcmp(&s->groups[i].key, k, sizeof(*k))) return &s->groups[i];
  return NULL;
}

/* One row; returns the LAD, or 0 when either side is missing or too small. */
static double lad_row(FILE* f, const char* desc, int indent, struct i64vec* bv,
                      struct i64vec* lv) {
  struct ntp_summary b, l;
  double lad = 0;

  ntp_summarize(bv, &b);
  ntp_summarize(lv, &l);
  if (!b.n || !l.n) return 0;
  fprintf(f, "  %*s%-*s %9.2f %9.2f", indent, "", NAME_W - indent, desc, us(b.mean),
          us(l.mean));
  if (b.mean >= LAD_MIN_BASE_NS) {
    lad = l.mean / b.mean;
    fprintf(f, " %7.2f\n", lad);
  } else {
    fprintf(f, " %7s\n", "n/a");
  }
  return lad;
}

static void compare_group(FILE* f, struct ntp_group* bg, struct ntp_group* lg,
                          const struct ntp_report_opts* o) {
  bool joined = group_joined(bg) && group_joined(lg);
  struct srow br[MAX_SROWS], lr[MAX_SROWS];
  int nb = stage_rows(bg, joined, br), nl = stage_rows(lg, joined, lr);
  const struct srow* best_row = NULL;
  double best = 0;
  char label[96];

  ntp_group_label(&lg->key, label, sizeof(label));
  fprintf(f, "\n== %s: baseline %llu commands sampled, loaded %llu\n", label,
          (unsigned long long)bg->records, (unsigned long long)lg->records);
  fprintf(f, "  %-*s %9s %9s %7s\n", NAME_W, "stage", "base us", "load us", "LAD");
  for (int i = 0; i < nl; i++) {
    double lad;
    int j = 0;

    /* the same stage in the baseline (the two runs may show different rows) */
    while (j < nb && strcmp(br[j].key, lr[i].key)) j++;
    if (j == nb) continue;
    lad = lad_row(f, lr[i].desc, 0, br[j].v, lr[i].v);
    if (lad > best) {
      best = lad;
      best_row = &lr[i];
    }
  }
  lad_row(f, ntp_comps[NTP_C_E2E].desc, 0, &bg->comp[NTP_C_E2E],
          &lg->comp[NTP_C_E2E]);
  if (best_row) fprintf(f, "  largest LAD: %s, %.2f\n", best_row->desc, best);
  if (!o->detail) return;
  fprintf(f, "  components\n");
  for (size_t d = 0; d < ARRAY_SIZE(display); d++) {
    enum ntp_comp c = display[d].comp;

    if (c == NTP_C_E2E) continue;
    lad_row(f, ntp_comps[c].desc, display[d].indent, &bg->comp[c], &lg->comp[c]);
  }
}

void ntp_report_compare(FILE* f, struct ntp_stats* base,
                        struct ntp_stats* loaded,
                        const struct ntp_report_opts* o) {
  uint64_t total = total_records(loaded);
  size_t* idx = order_groups(loaded);
  int shown = 0, skipped = 0;

  for (size_t i = 0; i < loaded->n; i++) {
    struct ntp_group* lg = &loaded->groups[idx[i]];
    struct ntp_group* bg = find_group(base, &lg->key);

    if (!bg) continue;
    if (folded(loaded, idx, i, total, o)) {
      skipped++;
      continue;
    }
    compare_group(f, bg, lg, o);
    shown++;
  }
  if (!shown)
    fprintf(f, "no group (I/O type, size, device) is present in both runs\n");
  else if (skipped)
    fprintf(f, "+ %d more group%s in both runs, under %d %% of the records "
               "each; --all-groups shows them\n",
            skipped, skipped > 1 ? "s" : "", MINOR_GROUP_PCT);
  free(idx);
}
