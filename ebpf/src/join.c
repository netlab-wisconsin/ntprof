// SPDX-License-Identifier: GPL-2.0
#include "join.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/*
 * Records waiting for their counterpart, per (cntlid, qid, cid), in arrival
 * order. Buckets exist only while they hold records, so memory follows the
 * number of unmatched records, not the key space.
 */
struct bucket {
  uint64_t key;
  struct bucket* next;
  size_t len, cap;
  unsigned char recs[]; /* len records of the table's size; [0] is the oldest */
};

struct table {
  struct bucket** v;
  size_t nbuckets; /* a power of 2 */
  size_t size;     /* bytes per record */
  size_t id_off;   /* offset of the record's struct ntp_cmd_id */
  size_t ts_off;   /* offset of the timestamp that ages it */
  size_t pending;
};

struct ntp_joiner {
  struct table targets;
  struct table hosts; /* host records that arrived before their target's */
  size_t max_per_key;
  struct ntp_join_stats st;
};

static uint64_t key_of(const struct ntp_cmd_id* id) {
  return (uint64_t)id->cntlid << 32 | (uint64_t)id->qid << 16 | id->cid;
}

static bool same_command(const struct ntp_cmd_id* a, const struct ntp_cmd_id* b) {
  return a->opcode == b->opcode && a->nsid == b->nsid && a->slba == b->slba &&
         a->nlb == b->nlb;
}

static void table_init(struct table* t, size_t size, size_t id_off,
                       size_t ts_off) {
  t->nbuckets = 1 << 16;
  t->v = xcalloc(t->nbuckets, sizeof(*t->v));
  t->size = size;
  t->id_off = id_off;
  t->ts_off = ts_off;
  t->pending = 0;
}

static void table_free(struct table* t) {
  for (size_t i = 0; i < t->nbuckets; i++) {
    struct bucket* b = t->v[i];

    while (b) {
      struct bucket* next = b->next;

      free(b);
      b = next;
    }
  }
  free(t->v);
}

static unsigned char* rec_at(const struct table* t, struct bucket* b, size_t i) {
  return b->recs + i * t->size;
}

static const struct ntp_cmd_id* id_of(const struct table* t, const void* rec) {
  return (const struct ntp_cmd_id*)((const unsigned char*)rec + t->id_off);
}

/* Pointer to the link that points at the bucket of @key (or at NULL). */
static struct bucket** lookup(struct table* t, uint64_t key) {
  uint64_t h = key;
  struct bucket** link;

  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  link = &t->v[h & (t->nbuckets - 1)];
  while (*link && (*link)->key != key) link = &(*link)->next;
  return link;
}

/* Append @rec; returns 1 if the oldest record of its key was dropped for it. */
static int table_push(struct table* t, const void* rec, size_t max_per_key) {
  uint64_t key = key_of(id_of(t, rec));
  struct bucket** link = lookup(t, key);
  struct bucket* b = *link;
  int dropped = 0;

  if (!b) {
    b = xmalloc(sizeof(*b) + t->size);
    b->key = key;
    b->len = 0;
    b->cap = 1;
    b->next = NULL;
    *link = b;
  } else if (max_per_key && b->len == max_per_key) {
    memmove(rec_at(t, b, 0), rec_at(t, b, 1), (b->len - 1) * t->size);
    b->len--;
    t->pending--;
    dropped = 1;
  } else if (b->len == b->cap) {
    struct bucket* next = b->next;

    b->cap *= 2;
    if (max_per_key && b->cap > max_per_key) b->cap = max_per_key;
    b = xrealloc(b, sizeof(*b) + b->cap * t->size);
    b->next = next;
    *link = b;
  }
  memcpy(rec_at(t, b, b->len++), rec, t->size);
  t->pending++;
  return dropped;
}

/* Remove the oldest record of command @id into @out; returns 1 if found. */
static int table_take(struct table* t, const struct ntp_cmd_id* id, void* out) {
  struct bucket** link = lookup(t, key_of(id));
  struct bucket* b = *link;

  for (size_t i = 0; b && i < b->len; i++) {
    if (!same_command(id_of(t, rec_at(t, b, i)), id)) continue;
    memcpy(out, rec_at(t, b, i), t->size);
    memmove(rec_at(t, b, i), rec_at(t, b, i + 1), (b->len - i - 1) * t->size);
    if (--b->len == 0) {
      *link = b->next;
      free(b);
    }
    t->pending--;
    return 1;
  }
  return 0;
}

/* Remove records whose aging timestamp is before @before_ns; each goes to @cb. */
static size_t table_expire(struct table* t, uint64_t before_ns,
                           void (*cb)(void* ctx, const void* rec), void* ctx) {
  size_t dropped = 0;

  for (size_t s = 0; s < t->nbuckets; s++) {
    struct bucket** link = &t->v[s];

    while (*link) {
      struct bucket* b = *link;
      size_t keep = 0;

      for (size_t i = 0; i < b->len; i++) {
        unsigned char* r = rec_at(t, b, i);
        uint64_t ts;

        memcpy(&ts, r + t->ts_off, sizeof(ts));
        if (ts < before_ns) {
          if (cb) cb(ctx, r);
          dropped++;
        } else {
          if (keep != i) memcpy(rec_at(t, b, keep), r, t->size);
          keep++;
        }
      }
      t->pending -= b->len - keep;
      b->len = keep;
      if (!keep) {
        *link = b->next;
        free(b);
      } else {
        link = &b->next;
      }
    }
  }
  return dropped;
}

struct ntp_joiner* ntp_joiner_new(size_t max_per_key) {
  struct ntp_joiner* j = xcalloc(1, sizeof(*j));

  table_init(&j->targets, sizeof(struct ntp_tgt_rec),
             offsetof(struct ntp_tgt_rec, id),
             offsetof(struct ntp_tgt_rec, ts[NTP_T_CMD_ARR]));
  table_init(&j->hosts, sizeof(struct ntp_host_rec),
             offsetof(struct ntp_host_rec, id),
             offsetof(struct ntp_host_rec, ts[NTP_H_BLK_DONE]));
  j->max_per_key = max_per_key; /* 0: unbounded */
  return j;
}

void ntp_joiner_free(struct ntp_joiner* j) {
  if (!j) return;
  table_free(&j->targets);
  table_free(&j->hosts);
  free(j);
}

void ntp_joiner_add_target(struct ntp_joiner* j, const struct ntp_tgt_rec* t) {
  /* a dropped record's host record never came (not sampled there) */
  j->st.targets_evicted += table_push(&j->targets, t, j->max_per_key);
  j->st.targets_added++;
}

int ntp_joiner_match(struct ntp_joiner* j, const struct ntp_host_rec* h,
                     struct ntp_tgt_rec* out) {
  if (table_take(&j->targets, &h->id, out)) {
    j->st.matched++;
    return 1;
  }
  j->st.host_unmatched++;
  return 0;
}

int ntp_joiner_target_arrives(struct ntp_joiner* j, const struct ntp_tgt_rec* t,
                              struct ntp_host_rec* host) {
  if (table_take(&j->hosts, &t->id, host)) {
    j->st.targets_added++;
    j->st.matched++;
    return 1;
  }
  ntp_joiner_add_target(j, t);
  return 0;
}

int ntp_joiner_host_arrives(struct ntp_joiner* j, const struct ntp_host_rec* h,
                            struct ntp_tgt_rec* out) {
  if (table_take(&j->targets, &h->id, out)) {
    j->st.matched++;
    return 1;
  }
  if (table_push(&j->hosts, h, j->max_per_key)) j->st.host_unmatched++;
  return 0;
}

struct release_ctx {
  ntp_host_cb cb;
  void* ctx;
};

static void release_one(void* ctx, const void* rec) {
  struct release_ctx* r = ctx;

  r->cb(r->ctx, rec);
}

size_t ntp_joiner_release_hosts(struct ntp_joiner* j, uint64_t before_ns,
                                ntp_host_cb cb, void* ctx) {
  struct release_ctx r = {cb, ctx};
  size_t n = table_expire(&j->hosts, before_ns, cb ? release_one : NULL, &r);

  j->st.host_unmatched += n;
  return n;
}

size_t ntp_joiner_expire(struct ntp_joiner* j, uint64_t before_ns) {
  size_t dropped = table_expire(&j->targets, before_ns, NULL, NULL);

  j->st.targets_expired += dropped;
  return dropped;
}

size_t ntp_joiner_pending(const struct ntp_joiner* j) { return j->targets.pending; }

size_t ntp_joiner_waiting_hosts(const struct ntp_joiner* j) {
  return j->hosts.pending;
}

const struct ntp_join_stats* ntp_joiner_stats(const struct ntp_joiner* j) {
  return &j->st;
}
