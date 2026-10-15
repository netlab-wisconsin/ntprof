/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Join host and target records of the same command.
 *
 * Target records are kept per (cntlid, qid, cid) in arrival order. A host
 * record takes the oldest target record under its key whose opcode, nsid,
 * slba and nlb also match. The cid alone is not enough: blk-mq reuses tags,
 * and nvme folds only a 4-bit generation counter into the cid.
 *
 * Offline, every target record is added before the host records are matched.
 * Online (role both), records come in emission order, and a target record
 * can come after its host record (SPDK's target emits once the response's
 * send completed); such a host record waits in the joiner until its target
 * record arrives or it is released unmatched.
 */
#ifndef NTPROF_JOIN_H
#define NTPROF_JOIN_H

#include <stdbool.h>
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

/* ---- online ---- */

/*
 * A host record arrives: returns 1 and fills @out with its target record, or
 * 0 after keeping @h until its target record arrives or it is released.
 */
int ntp_joiner_host_arrives(struct ntp_joiner* j, const struct ntp_host_rec* h,
                            struct ntp_tgt_rec* out);

/*
 * A target record arrives: returns 1 and fills @host with the waiting host
 * record it belongs to, or 0 after keeping @t.
 */
int ntp_joiner_target_arrives(struct ntp_joiner* j, const struct ntp_tgt_rec* t,
                              struct ntp_host_rec* host);

typedef void (*ntp_host_cb)(void* ctx, const struct ntp_host_rec* h);

/*
 * Give up on waiting host records that completed (NTP_H_BLK_DONE) before
 * @before_ns: each goes to @cb, unmatched. Returns how many.
 */
size_t ntp_joiner_release_hosts(struct ntp_joiner* j, uint64_t before_ns,
                                ntp_host_cb cb, void* ctx);

/* Number of host records waiting for their target record. */
size_t ntp_joiner_waiting_hosts(const struct ntp_joiner* j);

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
