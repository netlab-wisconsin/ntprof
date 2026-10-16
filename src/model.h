/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The stage model: turn one host record (and, when joined, its target record)
 * into latency components that add up to the host's end-to-end time.
 *
 * Only differences between timestamps of the same machine are taken. The
 * network share of a round trip is what is left of the host's wait after the
 * target's residence time is subtracted, as in the NSDI'25 paper.
 * Over loopback it is close to zero and may be slightly negative: the
 * target can see the packet before the host's sendmsg returns.
 */
#ifndef NTPROF_MODEL_H
#define NTPROF_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "ntprof.h"

enum ntp_io_class {
  NTP_IO_CLASS_READ = 0,
  NTP_IO_CLASS_WRITE_INLINE, /* data in the command capsule */
  NTP_IO_CLASS_WRITE_R2T,    /* data solicited by R2T */
  NTP_IO_CLASS_NR,
};

/* Components, in path order. Their sum is the end-to-end latency. */
enum ntp_comp {
  NTP_C_BLK_SUB = 0, /* S1 (S)    blk-mq submission                       */
  NTP_C_TR_SUB,      /* S2 (S)    initiator transport submission          */
  NTP_C_NET,         /* S3-S5     wire + both stacks' unaccounted time     */
  NTP_C_TGT_RX,      /* S5 (S)    target receive stack to transport        */
  NTP_C_TGT_SUB,     /* S6 (S)    target transport submission              */
  NTP_C_TGT_BE,      /* S7-S9     target block layer + device              */
  NTP_C_TGT_CMPL,    /* S6 (C)    target transport completion              */
  NTP_C_HOST_RX,     /* S3 (C)    initiator receive stack to transport     */
  NTP_C_HOST_WAIT,   /* S3-S5 (C) TCP read: waiting for the rest of the data */
  NTP_C_HOST_DATA,   /* S2 (C)    TCP read: transport processing the data   */
  NTP_C_TR_CMPL,     /* S2 (C)    initiator transport completion           */
  NTP_C_BLK_CMPL,    /* S1 (C)    blk-mq completion                        */
  NTP_C_REMOTE,      /* S3-S9     without a target record: NET..TGT_CMPL   */
  NTP_C_E2E,         /*           end to end                               */
  /* details that split a component above */
  NTP_C_TGT_BE_SUB,  /* S7 (S)    target blk-mq submission (bdev)          */
  NTP_C_TGT_DEV,     /* S8-S9     target device (bdev)                     */
  NTP_C_TGT_BE_CMPL, /* S7 (C)    target blk-mq completion (bdev)          */
  NTP_C_CMPL_IPI,    /*           CQE parsed -> nvme_complete_rq()         */
  NTP_C_NET_DATA,    /* S3-S5     RDMA: the data over the network, timed by
                                  the target (write: its RDMA READ; read: the
                                  RDMA WRITE + response, posting to send
                                  completion, which includes the ACK back)  */
  NTP_C_NR,
};

struct ntp_comp_info {
  const char* name;  /* stable identifier, used in JSON */
  const char* stage; /* the stage it belongs to (ntp_stage_keys), used in JSON */
  const char* desc;  /* what users read */
  bool detail;       /* splits another component */
};

extern const struct ntp_comp_info ntp_comps[NTP_C_NR];

/*
 * Stage groups for LAD: each is a sum of components. The NSDI'25 paper calls
 * them S1, S2, S3-S5, S6 and S7-S9; users see the names in ntp_stage_keys.
 */
enum ntp_stage {
  NTP_S1 = 0,   /* host block layer            */
  NTP_S2,       /* host driver                 */
  NTP_S3_S5,    /* network (and receive paths) */
  NTP_S6,       /* target driver               */
  NTP_S7_S9,    /* storage: target block layer + drive */
  NTP_STAGE_NR,
};

/* host_block_layer, host_driver, network, target_driver, storage */
extern const char* const ntp_stage_keys[NTP_STAGE_NR];

struct ntp_breakdown {
  enum ntp_io_class io_class;
  bool joined;    /* a target record was used */
  bool post_estimated; /* RDMA: the response's posting time is nvmet_req_complete's */
  uint32_t valid; /* bit i set: comp[i] holds a value */
  int64_t comp[NTP_C_NR];
  int64_t stage[NTP_STAGE_NR];
  uint32_t stage_valid;
};

enum ntp_model_err {
  NTP_MODEL_OK = 0,
  NTP_MODEL_INCOMPLETE_HOST,   /* a required host event is missing */
  NTP_MODEL_INCOMPLETE_TARGET, /* joined, but a required target event is missing */
};

enum ntp_io_class ntp_io_class_of(const struct ntp_host_rec* h);

/*
 * Compute the breakdown. @t may be NULL (host only). With a target record
 * that misses events, falls back to the host-only form and returns
 * NTP_MODEL_INCOMPLETE_TARGET; a host record without the events its class
 * needs gives NTP_MODEL_INCOMPLETE_HOST and no values.
 */
enum ntp_model_err ntp_breakdown(const struct ntp_host_rec* h,
                                 const struct ntp_tgt_rec* t,
                                 struct ntp_breakdown* out);

static inline bool ntp_comp_valid(const struct ntp_breakdown* b,
                                  enum ntp_comp c) {
  return b->valid & (1u << c);
}

#endif /* NTPROF_MODEL_H */
