// SPDX-License-Identifier: GPL-2.0
#include "stats.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const double ntp_quantiles[NTP_NQ] = {0.10, 0.50, 0.70, 0.80,
                                      0.90, 0.95, 0.99, 0.999};
const char* const ntp_quantile_names[NTP_NQ] = {"p10", "p50", "p70", "p80",
                                                "p90", "p95", "p99", "p99.9"};
const char* const ntp_queue_names[NTP_Q_NR] = {
    [NTP_Q_HOST_UNSENT] = "host_unsent",
    [NTP_Q_HOST_INFLIGHT] = "host_inflight",
    [NTP_Q_TGT_OUTSTANDING] = "target_outstanding",
    [NTP_Q_TGT_RESP_QUEUE] = "target_resp_queue",
};

static int cmp_i64(const void* a, const void* b) {
  int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;

  return (x > y) - (x < y);
}

/* Linear interpolation between closest ranks, as v1's calculate_quantiles(). */
void ntp_summarize(struct i64vec* v, struct ntp_summary* out) {
  long double sum = 0;

  memset(out, 0, sizeof(*out));
  out->n = v->n;
  if (!v->n) return;
  qsort(v->v, v->n, sizeof(*v->v), cmp_i64);
  for (size_t i = 0; i < v->n; i++) sum += v->v[i];
  out->mean = (double)(sum / v->n);
  out->min = v->v[0];
  out->max = v->v[v->n - 1];
  for (int i = 0; i < NTP_NQ; i++) {
    double pos = ntp_quantiles[i] * (double)(v->n - 1);
    size_t lo = (size_t)pos;
    double frac = pos - (double)lo;

    if (lo + 1 >= v->n)
      out->q[i] = v->v[v->n - 1];
    else
      out->q[i] = v->v[lo] + (int64_t)((double)(v->v[lo + 1] - v->v[lo]) * frac +
                                       0.5);
  }
}

void ntp_session_name(const char* disk, char* out, size_t len) {
  const char* c = disk;

  /* nvme<ctrl/subsys>c<ctrl>n<ns>: drop the c<ctrl> part */
  if (!strncmp(c, "nvme", 4)) {
    c += 4;
    while (isdigit((unsigned char)*c)) c++;
    if (*c == 'c' && isdigit((unsigned char)c[1])) {
      const char* n = c + 1;

      while (isdigit((unsigned char)*n)) n++;
      if (*n == 'n') {
        snprintf(out, len, "%.*s%s", (int)(c - disk), disk, n);
        return;
      }
    }
  }
  snprintf(out, len, "%s", disk);
}

void ntp_group_label(const struct ntp_group_key* k, char* out, size_t len) {
  int n = 0;

  out[0] = '\0';
  if (k->is_write >= 0)
    n += snprintf(out + n, len - n, "%s", k->is_write ? "write" : "read");
  if (k->bytes && n < (int)len)
    n += snprintf(out + n, len - n, "%s%uB", n ? " " : "", k->bytes);
  if (k->session[0] && n < (int)len)
    n += snprintf(out + n, len - n, "%s%s", n ? " " : "", k->session);
  if (!n) snprintf(out, len, "all");
}

void ntp_stats_init(struct ntp_stats* s, bool by_type, bool by_size,
                    bool by_session) {
  memset(s, 0, sizeof(*s));
  s->by_type = by_type;
  s->by_size = by_size;
  s->by_session = by_session;
}

static void group_free(struct ntp_group* g) {
  for (int i = 0; i < NTP_C_NR; i++) i64vec_free(&g->comp[i]);
  for (int i = 0; i < NTP_STAGE_NR; i++) i64vec_free(&g->stage[i]);
  for (int i = 0; i < NTP_Q_NR; i++) i64vec_free(&g->queue[i]);
}

void ntp_stats_free(struct ntp_stats* s) {
  for (size_t i = 0; i < s->n; i++) group_free(&s->groups[i]);
  free(s->groups);
  s->groups = NULL;
  s->n = s->cap = 0;
}

void ntp_stats_reset(struct ntp_stats* s) {
  bool t = s->by_type, z = s->by_size, d = s->by_session;

  ntp_stats_free(s);
  ntp_stats_init(s, t, z, d);
}

static struct ntp_group* group_for(struct ntp_stats* s,
                                   const struct ntp_host_rec* h) {
  struct ntp_group_key k;

  memset(&k, 0, sizeof(k));
  k.is_write = s->by_type ? !!(h->hdr.flags & NTP_F_WRITE) : -1;
  k.bytes = s->by_size ? h->bytes : 0;
  if (s->by_session) ntp_session_name(h->disk, k.session, sizeof(k.session));

  for (size_t i = 0; i < s->n; i++)
    if (!memcmp(&s->groups[i].key, &k, sizeof(k))) return &s->groups[i];

  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 8;
    s->groups = xrealloc(s->groups, s->cap * sizeof(*s->groups));
  }
  memset(&s->groups[s->n], 0, sizeof(s->groups[s->n]));
  s->groups[s->n].key = k;
  return &s->groups[s->n++];
}

void ntp_stats_add(struct ntp_stats* s, const struct ntp_host_rec* h,
                   const struct ntp_tgt_rec* t) {
  struct ntp_group* g = group_for(s, h);
  struct ntp_breakdown b;
  enum ntp_model_err err = ntp_breakdown(h, t, &b);

  g->records++;
  if (err == NTP_MODEL_INCOMPLETE_HOST) {
    g->bad_host++;
    return;
  }
  if (err == NTP_MODEL_INCOMPLETE_TARGET) g->bad_target++;
  if (b.joined) g->joined++;

  for (int i = 0; i < NTP_C_NR; i++)
    if (ntp_comp_valid(&b, i)) i64vec_push(&g->comp[i], b.comp[i]);
  for (int i = 0; i < NTP_STAGE_NR; i++)
    if (b.stage_valid & (1u << i)) i64vec_push(&g->stage[i], b.stage[i]);

  i64vec_push(&g->queue[NTP_Q_HOST_UNSENT], h->q_unsent);
  i64vec_push(&g->queue[NTP_Q_HOST_INFLIGHT], h->q_inflight);
  if (b.joined) {
    i64vec_push(&g->queue[NTP_Q_TGT_OUTSTANDING], t->q_outstanding);
    i64vec_push(&g->queue[NTP_Q_TGT_RESP_QUEUE], t->q_resp_queue);
  }
}
