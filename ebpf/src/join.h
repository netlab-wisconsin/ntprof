/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Join host and target records of the same command.
 *
 * Target records are kept per (cntlid, qid, cid) in arrival order. A host
 * record takes the oldest target record under its key whose opcode, nsid,
 * slba and nlb also match. The cid alone is not enough: blk-mq reuses tags,
 * and nvme folds only a 4-bit generation counter into the cid.
 */
#ifndef NTPROF_JOIN_H
#define NTPROF_JOIN_H

#include <stddef.h>
#include <stdint.h>

#include "ntprof.h"

struct ntp_joiner;

struct ntp_join_stats {
  uint64_t targets_added;
  uint64_t targets_evicted; /* dropped unmatched to bound memory */
  uint64_t targets_expired; /* dropped unmatched by age */
  uint64_t matched;
  uint64_t host_unmatched;
};

/*
 * @max_per_key bounds the unmatched target records kept under one key; the
 * oldest is dropped beyond it. 0 keeps them all (offline analysis).
 */
struct ntp_joiner* ntp_joiner_new(size_t max_per_key);
void ntp_joiner_free(struct ntp_joiner* j);

void ntp_joiner_add_target(struct ntp_joiner* j, const struct ntp_tgt_rec* t);

/*
 * Find (and remove) the target record of @h. Returns 1 and fills @out, or 0
 * when there is none.
 */
int ntp_joiner_match(struct ntp_joiner* j, const struct ntp_host_rec* h,
                     struct ntp_tgt_rec* out);

/*
 * Drop target records that arrived before @before_ns (their T_CMD_ARR). Only
 * meaningful when host and target share a clock (one machine). Returns the
 * number dropped.
 */
size_t ntp_joiner_expire(struct ntp_joiner* j, uint64_t before_ns);

/* Number of target records still waiting for their host record. */
size_t ntp_joiner_pending(const struct ntp_joiner* j);

const struct ntp_join_stats* ntp_joiner_stats(const struct ntp_joiner* j);

#endif /* NTPROF_JOIN_H */
