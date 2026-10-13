/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ntprof v2: definitions shared by the BPF programs and the user-space tool.
 *
 * Each sampled NVMe-oF command produces one record per side: a host record on
 * the initiator (blk-mq to completion) and a target record on the target
 * (command arrival to response). The two never travel in the PDUs; user space
 * joins them on (cntlid, qid, cid) and checks opcode, nsid, slba and nlb.
 *
 * Every timestamp is CLOCK_MONOTONIC in ns (bpf_ktime_get_ns), so differences
 * are only taken between timestamps of the same side. A timestamp of 0 means
 * the event was not seen (not applicable, or its hook is not available on
 * this kernel).
 */
#ifndef NTPROF_H
#define NTPROF_H

#ifndef __bpf__
#include <linux/types.h>
#endif

#define NTP_DISK_NAME_LEN 32
#define NTP_MAX_SIZE_FILTERS 16

enum ntp_rec_kind {
  NTP_REC_HOST = 1,
  NTP_REC_TARGET = 2,
};

enum ntp_transport {
  NTP_TR_TCP = 1,
  NTP_TR_RDMA = 2,
};

/*
 * Host (initiator) events. The names follow the I/O through the stages of the
 * NSDI'25 model (S1-S9); see ebpf/DESIGN.md for the hook behind each one.
 */
enum ntp_host_ts {
  NTP_H_START = 0,    /* blk-mq request start (rq->start_time_ns)          */
  NTP_H_QUEUE_RQ,     /* transport ->queue_rq() entered                    */
  NTP_H_CMD_SENT,     /* command capsule (and in-capsule data) handed off  */
  NTP_H_R2T_ARR,      /* skb carrying the first R2T reached the host stack */
  NTP_H_R2T_PROC,     /* first R2T parsed by the transport                 */
  NTP_H_DATA_START,   /* sending of the solicited H2C data began           */
  NTP_H_DATA_SENT,    /* all solicited H2C data handed off                 */
  NTP_H_C2H_ARR,      /* skb carrying the first C2H data PDU arrived       */
  NTP_H_C2H_PROC,     /* first C2H data PDU parsed by the transport        */
  NTP_H_RSP_ARR,      /* skb carrying the response capsule arrived         */
  NTP_H_RSP_PROC,     /* completion parsed, request handed to blk-mq       */
  NTP_H_NVME_DONE,    /* nvme_complete_rq() (after a possible IPI)         */
  NTP_H_BLK_DONE,     /* block_rq_complete                                 */
  NTP_H_NR,
};

/* Target events. */
enum ntp_tgt_ts {
  NTP_T_CMD_ARR = 0,  /* skb completing the command capsule header arrived */
  NTP_T_CMD_PROC,     /* nvmet_req_init()                                  */
  NTP_T_R2T_QUEUED,   /* R2T queued for sending                            */
  NTP_T_R2T_SENT,     /* R2T handed to the socket / posted                 */
  NTP_T_DATA_ARR,     /* skb with the first solicited data PDU arrived     */
  NTP_T_DATA_PROC,    /* first solicited data PDU parsed                   */
  NTP_T_EXEC,         /* backend execute (bdev / file / passthru)          */
  NTP_T_BE_ISSUE,     /* backend block request issued (bdev only)          */
  NTP_T_BE_DONE,      /* backend block request completed (bdev only)       */
  NTP_T_COMPLETE,     /* nvmet_req_complete()                              */
  NTP_T_RSP_QUEUED,   /* completion queued to the transport                */
  NTP_T_C2H_SENT,     /* first C2H data PDU handed to the socket / posted  */
  NTP_T_RSP_SENT,     /* response capsule handed to the socket / posted    */
  NTP_T_NR,
};

/* What identifies a command on the wire, on both sides. */
struct ntp_cmd_id {
  __u64 slba;
  __u32 nsid;
  __u16 cntlid;
  __u16 qid;
  __u16 cid;
  __u16 nlb; /* number of logical blocks, 0's based as on the wire */
  __u8 opcode;
  __u8 pad[3];
};

struct ntp_rec_hdr {
  __u8 kind;      /* enum ntp_rec_kind */
  __u8 transport; /* enum ntp_transport */
  __u16 flags;    /* NTP_F_* */
  __u32 cpu;      /* CPU that emitted the record */
};

#define NTP_F_WRITE (1u << 0)      /* data direction host -> target */
#define NTP_F_INLINE_DATA (1u << 1) /* write carried in the command capsule */
#define NTP_F_R2T (1u << 2)        /* write solicited by R2T (TCP) */
#define NTP_F_DATA_SUCCESS (1u << 3) /* read completed by C2H SUCCESS flag */
#define NTP_F_MULTI_R2T (1u << 4)  /* more than one R2T for this command */

struct ntp_host_rec {
  struct ntp_rec_hdr hdr;
  struct ntp_cmd_id id;
  __u32 bytes;         /* blk_rq_bytes() */
  __u32 ctrl_instance; /* N in /dev/nvmeN */
  __u16 q_unsent;      /* commands of this queue not yet sent, at arrival */
  __u16 q_inflight;    /* commands of this queue not yet completed, at arrival */
  __u32 pad;
  char disk[NTP_DISK_NAME_LEN];
  __u64 ts[NTP_H_NR];
};

struct ntp_tgt_rec {
  struct ntp_rec_hdr hdr;
  struct ntp_cmd_id id;
  __u32 bytes;         /* transfer length */
  __u16 q_outstanding; /* commands of this queue not yet answered, at arrival */
  __u16 q_resp_queue;  /* responses of this queue waiting to be sent, at arrival */
  __u64 ts[NTP_T_NR];
};

/* Per-CPU counters exported to user space for throughput (all commands). */
enum ntp_counter {
  NTP_CNT_HOST_READ_IOS = 0,
  NTP_CNT_HOST_READ_BYTES,
  NTP_CNT_HOST_WRITE_IOS,
  NTP_CNT_HOST_WRITE_BYTES,
  NTP_CNT_HOST_SAMPLED,
  NTP_CNT_HOST_EMITTED,
  NTP_CNT_TGT_CMDS,
  NTP_CNT_TGT_SAMPLED,
  NTP_CNT_TGT_EMITTED,
  NTP_CNT_RB_DROPS,      /* ring buffer full: record lost */
  NTP_CNT_MAP_FULL,      /* record map full: command not sampled */
  NTP_CNT_STALE,         /* unfinished record replaced by a new command */
  NTP_CNT_NR,
};

/*
 * Filter and sampling, set by user space in .rodata before the programs load.
 * A command is sampled when hash(cid, slba, nlb) % sample_every == 0; both
 * sides compute the same hash, so they sample the same commands without
 * exchanging a tag.
 */
struct ntp_filter {
  __u32 sample_every;  /* 1 = every command */
  __u8 io_type;        /* 0 = read, 1 = write, 2 = both */
  __u8 n_sizes;        /* exact sizes in sizes[]; 0 = any (subject to min/max) */
  __u8 ctrl_filter;    /* host: 1 = only controllers set in ctrl_mask */
  __u8 track_queues;   /* 1 = keep per-queue occupancy (costs map ops per I/O) */
  __u32 min_bytes;     /* 0 = no minimum */
  __u32 max_bytes;     /* 0 = no maximum */
  __u32 sizes[NTP_MAX_SIZE_FILTERS];
  __u64 ctrl_mask[4];  /* host: controller instances 0-255 (N of /dev/nvmeN) */
  __u32 nsid;          /* host: 0 = any namespace */
  __u32 ifindex;       /* skb arrival timestamps only from this device; 0 = any */
};

/* NVMe opcodes of the I/O command set used here. */
#define NTP_NVME_CMD_FLUSH 0x00
#define NTP_NVME_CMD_WRITE 0x01
#define NTP_NVME_CMD_READ 0x02

static inline __u64 ntp_sample_hash(__u16 cid, __u64 slba, __u16 nlb) {
  /* splitmix64 finaliser over the identifying fields */
  __u64 x = slba ^ ((__u64)cid << 48) ^ ((__u64)nlb << 32);
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

#endif /* NTPROF_H */
