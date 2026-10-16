/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ntprof: definitions shared by the BPF programs and the user-space tool.
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
#define NTP_MAX_PIDS 8

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
 * NSDI'25 model (S1-S9); see docs/design.md for the hook behind each one.
 * TCP: "handed off" = to the socket; RDMA: posted to the queue pair.
 */
enum ntp_host_ts {
  NTP_H_START = 0,    /* blk-mq request start (rq->start_time_ns)          */
  NTP_H_QUEUE_RQ,     /* transport ->queue_rq() entered                    */
  NTP_H_CMD_SENT,     /* command capsule (and in-capsule data) handed off  */
  NTP_H_R2T_ARR,      /* TCP: skb carrying the first R2T reached the stack */
  NTP_H_R2T_PROC,     /* TCP: first R2T parsed by the transport            */
  NTP_H_DATA_START,   /* TCP: sending of the solicited H2C data began      */
  NTP_H_DATA_SENT,    /* TCP: all solicited H2C data handed off            */
  NTP_H_C2H_ARR,      /* TCP: skb carrying the first C2H data PDU arrived  */
  NTP_H_C2H_PROC,     /* TCP: first C2H data PDU parsed by the transport   */
  NTP_H_RSP_ARR,      /* response arrived (TCP: its skb; RDMA: its CQE)    */
  NTP_H_RSP_PROC,     /* completion parsed, request handed to blk-mq       */
  NTP_H_NVME_DONE,    /* nvme_complete_rq() (after a possible IPI)         */
  NTP_H_BLK_DONE,     /* block_rq_complete                                 */
  NTP_H_SEND_DONE,    /* RDMA: send completion of the command capsule      */
  NTP_H_C2H_LAST_ARR, /* TCP: skb carrying the read's last data arrived    */
  NTP_H_C2H_DONE,     /* TCP: the transport finished the read's data       */
  NTP_H_NR,
};

/* Host events up to file version 3; later ones were added at the end. */
#define NTP_H_NR_V3 NTP_H_C2H_LAST_ARR

/* Target events. */
enum ntp_tgt_ts {
  NTP_T_CMD_ARR = 0,  /* command arrived (TCP: its skb; RDMA: its CQE)     */
  NTP_T_CMD_PROC,     /* nvmet_req_init()                                  */
  NTP_T_R2T_QUEUED,   /* TCP: R2T queued for sending                       */
  NTP_T_R2T_SENT,     /* R2T handed to the socket; RDMA: RDMA READ posted  */
  NTP_T_DATA_ARR,     /* first solicited data arrived (RDMA: READ done)    */
  NTP_T_DATA_PROC,    /* first solicited data parsed (RDMA: READ done)     */
  NTP_T_EXEC,         /* backend execute (bdev / file / passthru)          */
  NTP_T_BE_ISSUE,     /* backend block request issued (bdev only)          */
  NTP_T_BE_DONE,      /* backend block request completed (bdev only)       */
  NTP_T_COMPLETE,     /* nvmet_req_complete()                              */
  NTP_T_RSP_QUEUED,   /* completion queued to the transport                */
  NTP_T_C2H_SENT,     /* first C2H data PDU handed to the socket / posted  */
  NTP_T_RSP_SENT,     /* response capsule handed to the socket / posted    */
  NTP_T_SEND_DONE,    /* RDMA: the response's send completion (after the
                         data's RDMA WRITE, for a read)                     */
  NTP_T_DATA_LAST_ARR, /* TCP: skb carrying the write's last data arrived */
  NTP_T_DATA_DONE,    /* TCP: the transport copied the write's last data   */
  NTP_T_NR,
};

/* Target events up to file version 4; later ones were added at the end. */
#define NTP_T_NR_V4 NTP_T_DATA_LAST_ARR

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
#define NTP_F_QUEUES (1u << 5)     /* the q_* fields were tracked (exact mode) */
#define NTP_F_RDMA_READ (1u << 6)  /* RDMA: the target fetched the write data */
#define NTP_F_SPDK (1u << 7)       /* recorded in SPDK (user-space transport) */
#define NTP_F_REUSED (1u << 8)     /* target: emitted when its nvmet_req came back */

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
  /*
   * TCP reads: time the transport spent on the skbs that carried this read's
   * data (each nvme_tcp_recv_skb call, from the first data copy on), and how
   * many such calls. Within [NTP_H_C2H_PROC, NTP_H_C2H_DONE] the rest is the
   * host waiting for data. Fields after ts[] are new in file version 4.
   */
  __u64 c2h_busy_ns;
  __u32 c2h_skbs;
  __u32 pad2;
};

struct ntp_tgt_rec {
  struct ntp_rec_hdr hdr;
  struct ntp_cmd_id id;
  __u32 bytes;         /* transfer length */
  __u16 q_outstanding; /* commands of this queue not yet answered, at arrival */
  __u16 q_resp_queue;  /* responses of this queue waiting to be sent, at arrival */
  __u64 ts[NTP_T_NR];
  /*
   * TCP writes after R2T: time the transport spent copying this write's data
   * into its buffer (each skb_copy_datagram_iter call, the first from the PDU
   * header parsed on when the data came with it), and how many copies. Within
   * [NTP_T_DATA_PROC, NTP_T_DATA_DONE] the rest is the target waiting for
   * data. Fields after ts[] are new in file version 5.
   */
  __u64 h2c_busy_ns;
  __u32 h2c_skbs;
  __u32 pad2;
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
  NTP_CNT_STALE,         /* unfinished record replaced (error path, slot collision) */
  NTP_CNT_NR,
};

/* The value of the one-entry per-CPU counters map: one lookup per program. */
struct ntp_counters {
  __u64 c[NTP_CNT_NR];
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
  /*
   * CLOCK_REALTIME - CLOCK_MONOTONIC when ntprof started, in ns: turns an
   * skb's receive timestamp (realtime) into the clock of the records.
   */
  __s64 real_minus_mono;
  __u8 npids;          /* SPDK: entries in pids[] */
  __u8 pad2[3];
  __u32 pids[NTP_MAX_PIDS]; /* SPDK: processes whose socket reads are watched */
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
