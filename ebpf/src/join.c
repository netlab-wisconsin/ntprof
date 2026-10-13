// SPDX-License-Identifier: GPL-2.0
#include "join.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

/*
 * A FIFO of target records under one key. Buckets exist only while they hold
 * records, so memory follows the number of unmatched records, not the key
 * space (cntlid x qid x cid).
 */
struct bucket {
  uint64_t key;
  struct bucket* next;
  size_t len, cap;
  struct ntp_tgt_rec recs[]; /* recs[0] is the oldest */
};

struct ntp_joiner {
  struct bucket** table;
  size_t nbuckets;
  size_t max_per_key;
  size_t pending;
  struct ntp_join_stats st;
};

static uint64_t key_of(const struct ntp_cmd_id* id) {
  return (uint64_t)id->cntlid << 32 | (uint64_t)id->qid << 16 | id->cid;
}

static struct bucket** slot(struct ntp_joiner* j, uint64_t key) {
  uint64_t h = key;

  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return &j->table[h & (j->nbuckets - 1)];
}

/* Pointer to the link that points at the bucket of @key (or at NULL). */
static struct bucket** lookup(struct ntp_joiner* j, uint64_t key) {
  struct bucket** link = slot(j, key);

  while (*link && (*link)->key != key) link = &(*link)->next;
  return link;
}

static bool same_command(const struct ntp_cmd_id* a, const struct ntp_cmd_id* b) {
  return a->opcode == b->opcode && a->nsid == b->nsid && a->slba == b->slba &&
         a->nlb == b->nlb;
}

struct ntp_joiner* ntp_joiner_new(size_t max_per_key) {
  struct ntp_joiner* j = xcalloc(1, sizeof(*j));

  j->nbuckets = 1 << 16;
  j->table = xcalloc(j->nbuckets, sizeof(*j->table));
  j->max_per_key = max_per_key; /* 0: unbounded */
  return j;
}

void ntp_joiner_free(struct ntp_joiner* j) {
  if (!j) return;
  for (size_t i = 0; i < j->nbuckets; i++) {
    struct bucket* b = j->table[i];

    while (b) {
      struct bucket* next = b->next;

      free(b);
      b = next;
    }
  }
  free(j->table);
  free(j);
}

void ntp_joiner_add_target(struct ntp_joiner* j, const struct ntp_tgt_rec* t) {
  uint64_t key = key_of(&t->id);
  struct bucket** link = lookup(j, key);
  struct bucket* b = *link;

  if (!b) {
    b = xmalloc(sizeof(*b) + sizeof(b->recs[0]));
    b->key = key;
    b->len = 0;
    b->cap = 1;
    b->next = NULL;
    *link = b;
  } else if (j->max_per_key && b->len == j->max_per_key) {
    /* drop the oldest: its host record never came (not sampled there) */
    memmove(&b->recs[0], &b->recs[1], (b->len - 1) * sizeof(b->recs[0]));
    b->len--;
    j->pending--;
    j->st.targets_evicted++;
  } else if (b->len == b->cap) {
    struct bucket* next = b->next;

    b->cap *= 2;
    if (j->max_per_key && b->cap > j->max_per_key) b->cap = j->max_per_key;
    b = xrealloc(b, sizeof(*b) + b->cap * sizeof(b->recs[0]));
    b->next = next;
    *link = b;
  }
  b->recs[b->len++] = *t;
  j->pending++;
  j->st.targets_added++;
}

int ntp_joiner_match(struct ntp_joiner* j, const struct ntp_host_rec* h,
                     struct ntp_tgt_rec* out) {
  struct bucket** link = lookup(j, key_of(&h->id));
  struct bucket* b = *link;

  for (size_t i = 0; b && i < b->len; i++) {
    if (!same_command(&b->recs[i].id, &h->id)) continue;
    *out = b->recs[i];
    memmove(&b->recs[i], &b->recs[i + 1], (b->len - i - 1) * sizeof(b->recs[0]));
    if (--b->len == 0) {
      *link = b->next;
      free(b);
    }
    j->pending--;
    j->st.matched++;
    return 1;
  }
  j->st.host_unmatched++;
  return 0;
}

size_t ntp_joiner_expire(struct ntp_joiner* j, uint64_t before_ns) {
  size_t dropped = 0;

  for (size_t s = 0; s < j->nbuckets; s++) {
    struct bucket** link = &j->table[s];

    while (*link) {
      struct bucket* b = *link;
      size_t keep = 0;

      for (size_t i = 0; i < b->len; i++) {
        if (b->recs[i].ts[NTP_T_CMD_ARR] < before_ns)
          dropped++;
        else
          b->recs[keep++] = b->recs[i];
      }
      j->pending -= b->len - keep;
      b->len = keep;
      if (!keep) {
        *link = b->next;
        free(b);
      } else {
        link = &b->next;
      }
    }
  }
  j->st.targets_expired += dropped;
  return dropped;
}

size_t ntp_joiner_pending(const struct ntp_joiner* j) { return j->pending; }

const struct ntp_join_stats* ntp_joiner_stats(const struct ntp_joiner* j) {
  return &j->st;
}
