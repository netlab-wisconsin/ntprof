// SPDX-License-Identifier: GPL-2.0
#include "report.h"

#include <errno.h>
#include <string.h>

#include "util.h"

/* Display order: path order, details right after the component they split. */
static const struct {
  enum ntp_comp comp;
  int indent;
} display[] = {
    {NTP_C_BLK_SUB, 0},      {NTP_C_TR_SUB, 0},      {NTP_C_NET, 0},
    {NTP_C_TGT_RX, 0},       {NTP_C_TGT_SUB, 0},     {NTP_C_TGT_BE, 0},
    {NTP_C_TGT_BE_SUB, 2},   {NTP_C_TGT_DEV, 2},     {NTP_C_TGT_BE_CMPL, 2},
    {NTP_C_TGT_CMPL, 0},     {NTP_C_REMOTE, 0},      {NTP_C_HOST_RX, 0},
    {NTP_C_TR_CMPL, 0},      {NTP_C_BLK_CMPL, 0},    {NTP_C_CMPL_IPI, 2},
    {NTP_C_E2E, 0},
};

/* A stage mean below this is too small for a ratio to mean anything. */
#define LAD_MIN_BASE_NS 500.0

void ntp_report_opts_from_config(struct ntp_report_opts* o,
                                 const struct ntp_config* c) {
  o->aggr = c->aggr;
  o->distribution = c->rep_distribution;
  o->breakdown = c->rep_breakdown;
  o->queue_length = c->rep_queue_length;
}

void ntp_report_tput(FILE* f, const struct ntp_tput* t) {
  double s = t->seconds > 0 ? t->seconds : 1;

  fprintf(f,
          "throughput: read %.1f kIOPS %.1f MB/s | write %.1f kIOPS %.1f MB/s "
          "(%.1f s, all commands, as submitted)\n",
          t->read_ios / s / 1e3, t->read_bytes / s / 1e6, t->write_ios / s / 1e3,
          t->write_bytes / s / 1e6, t->seconds);
}

/* Which quantile columns to print. */
static int columns(const struct ntp_report_opts* o, int* idx) {
  int n = 0;

  if (!o->distribution) return 0;
  if (o->aggr == NTP_AGG_DIST) {
    for (int i = 0; i < NTP_NQ; i++) idx[n++] = i;
  } else {
    idx[n++] = 1; /* p50 */
    idx[n++] = 6; /* p99 */
  }
  return n;
}

static double us(double ns) { return ns / 1e3; }

static void print_header(FILE* f, const struct ntp_report_opts* o) {
  int q[NTP_NQ], nq = columns(o, q);

  fprintf(f, "  %-36s %-11s %8s %9s %7s", "component", "stage", "n", "mean", "L");
  if (o->aggr == NTP_AGG_MIN) fprintf(f, " %9s", "min");
  for (int i = 0; i < nq; i++) fprintf(f, " %9s", ntp_quantile_names[q[i]]);
  if (o->aggr == NTP_AGG_MAX) fprintf(f, " %9s", "max");
  fprintf(f, "   (us)\n");
}

/* @rate: commands per second, for L = rate x mean (Little's law); 0: none */
static void print_row(FILE* f, const char* label, int indent,
                      const char* stage, const struct ntp_summary* m,
                      double rate, const struct ntp_report_opts* o) {
  int q[NTP_NQ], nq = columns(o, q);

  fprintf(f, "  %*s%-*s %-11s %8zu %9.2f", indent, "", 36 - indent, label,
          stage, m->n, us(m->mean));
  if (rate > 0)
    fprintf(f, " %7.2f", rate * m->mean / 1e9);
  else
    fprintf(f, " %7s", "-");
  if (o->aggr == NTP_AGG_MIN) fprintf(f, " %9.2f", us((double)m->min));
  for (int i = 0; i < nq; i++) fprintf(f, " %9.2f", us((double)m->q[q[i]]));
  if (o->aggr == NTP_AGG_MAX) fprintf(f, " %9.2f", us((double)m->max));
  fputc('\n', f);
}

static void print_group(FILE* f, const struct ntp_stats* s, struct ntp_group* g,
                        const struct ntp_report_opts* o) {
  double rate = ntp_group_rate(s, g);
  struct ntp_summary m, e2e;
  char label[96];

  ntp_group_label(&g->key, label, sizeof(label));
  fprintf(f, "== %s: %llu records, %llu joined with the target", label,
          (unsigned long long)g->records, (unsigned long long)g->joined);
  if (g->bad_host || g->bad_target)
    fprintf(f, ", incomplete: %llu host / %llu target",
            (unsigned long long)g->bad_host, (unsigned long long)g->bad_target);
  fputc('\n', f);

  ntp_summarize(&g->comp[NTP_C_E2E], &e2e);
  if (!e2e.n) return;

  if (o->breakdown) {
    print_header(f, o);
    for (size_t i = 0; i < ARRAY_SIZE(display); i++) {
      enum ntp_comp c = display[i].comp;

      ntp_summarize(&g->comp[c], &m);
      if (!m.n) continue;
      print_row(f, ntp_comps[c].desc, display[i].indent, ntp_comps[c].stage, &m,
                rate, o);
    }
  } else {
    print_header(f, o);
    print_row(f, ntp_comps[NTP_C_E2E].desc, 0, ntp_comps[NTP_C_E2E].stage, &e2e,
              rate, o);
  }

  fprintf(f, "  stages (mean us, share of end to end):");
  for (int s = 0; s < NTP_STAGE_NR; s++) {
    ntp_summarize(&g->stage[s], &m);
    if (!m.n) continue;
    fprintf(f, " %s %.2f (%.0f%%)", ntp_stage_names[s], us(m.mean),
            e2e.mean > 0 ? 100.0 * m.mean / e2e.mean : 0.0);
  }
  fputc('\n', f);

  if (o->queue_length) {
    bool any = false;

    for (int q = 0; q < NTP_Q_NR; q++) {
      ntp_summarize(&g->queue[q], &m);
      if (!m.n) continue;
      fprintf(f, "%s %s %.2f/%lld", any ? "," : "  queue length at arrival (mean/p99):",
              ntp_queue_names[q], m.mean, (long long)m.q[6]);
      any = true;
    }
    if (any) fputc('\n', f);
  }
}

void ntp_report_text(FILE* f, struct ntp_stats* s,
                     const struct ntp_report_opts* o) {
  if (!s->n) {
    fprintf(f, "no sampled commands\n");
    return;
  }
  if (o->breakdown)
    fprintf(f, "(L: mean number of commands in the component, by Little's law "
               "= command rate x mean time)\n");
  for (size_t i = 0; i < s->n; i++) print_group(f, s, &s->groups[i], o);
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
  fprintf(f, "{\n  \"tool\": \"ntprof v2\",\n");
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
            "\"read_bytes\": %llu, \"write_ios\": %llu, \"write_bytes\": %llu},\n",
            tput->seconds, (unsigned long long)tput->read_ios,
            (unsigned long long)tput->read_bytes,
            (unsigned long long)tput->write_ios,
            (unsigned long long)tput->write_bytes);
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
            "\"incomplete_target\": %llu, \"rate_per_s\": %.1f,\n"
            "     \"components\": {",
            (unsigned long long)g->records, (unsigned long long)g->joined,
            (unsigned long long)g->bad_host, (unsigned long long)g->bad_target,
            rate);
    for (int c = 0; c < NTP_C_NR; c++) {
      ntp_summarize(&g->comp[c], &m);
      if (!m.n) continue;
      fprintf(f, "%s\n       \"%s\": {\"stage\": \"%s\", \"occupancy\": %.3f, "
                 "\"summary\": ",
              first ? "" : ",", ntp_comps[c].name, ntp_comps[c].stage,
              rate * m.mean / 1e9);
      json_summary(f, &m);
      fputc('}', f);
      first = false;
    }
    fprintf(f, "},\n     \"stages\": {");
    first = true;
    for (int st = 0; st < NTP_STAGE_NR; st++) {
      ntp_summarize(&g->stage[st], &m);
      if (!m.n) continue;
      fprintf(f, "%s\n       \"%s\": ", first ? "" : ",", ntp_stage_names[st]);
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

static void lad_row(FILE* f, const char* label, const char* stage,
                    struct i64vec* bv, struct i64vec* lv, double* best,
                    const char** best_label) {
  struct ntp_summary b, l;

  ntp_summarize(bv, &b);
  ntp_summarize(lv, &l);
  if (!b.n || !l.n) return;
  fprintf(f, "  %-36s %-11s %10.2f %10.2f", label, stage, us(b.mean), us(l.mean));
  if (b.mean >= LAD_MIN_BASE_NS) {
    double lad = l.mean / b.mean;

    fprintf(f, " %8.2f\n", lad);
    if (best && lad > *best) {
      *best = lad;
      *best_label = stage;
    }
  } else {
    fprintf(f, " %8s\n", "n/a");
  }
}

void ntp_report_compare(FILE* f, struct ntp_stats* base,
                        struct ntp_stats* loaded) {
  int shown = 0;

  for (size_t i = 0; i < loaded->n; i++) {
    struct ntp_group* lg = &loaded->groups[i];
    struct ntp_group* bg = find_group(base, &lg->key);
    const char* best_label = NULL;
    double best = 0;
    char label[96];

    if (!bg) continue;
    ntp_group_label(&lg->key, label, sizeof(label));
    fprintf(f, "== %s: baseline %llu records, loaded %llu records\n", label,
            (unsigned long long)bg->records, (unsigned long long)lg->records);
    fprintf(f, "  %-36s %-11s %10s %10s %8s\n", "stage / component", "",
            "base(us)", "load(us)", "LAD");
    for (int s = 0; s < NTP_STAGE_NR; s++)
      lad_row(f, "stage", ntp_stage_names[s], &bg->stage[s], &lg->stage[s],
              &best, &best_label);
    for (size_t d = 0; d < ARRAY_SIZE(display); d++) {
      enum ntp_comp c = display[d].comp;

      lad_row(f, ntp_comps[c].desc, ntp_comps[c].stage, &bg->comp[c],
              &lg->comp[c], NULL, NULL);
    }
    if (best_label)
      fprintf(f, "  largest stage LAD: %s (%.2f)\n", best_label, best);
    shown++;
  }
  if (!shown) fprintf(f, "no group is present in both runs\n");
}
