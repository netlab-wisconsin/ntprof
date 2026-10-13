/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Initiator side, common to every NVMe-oF transport: the request record from
 * blk-mq to block_rq_complete, keyed by its struct request, and the hooks in
 * the block layer and nvme-core.
 *
 * A transport object defines NTP_TRANSPORT and includes this file once, then
 * adds its hooks: the transport's ->queue_rq() calls host_queue_rq(), the
 * code that finishes sending the command stamps NTP_H_CMD_SENT, and the code
 * that receives completions sets the receive context (host_rx_enter/exit)
 * around the call that completes the request.
 */
#ifndef NTPROF_HOST_BPF_H
#define NTPROF_HOST_BPF_H

#include "common.bpf.h"

#ifndef NTP_TRANSPORT
#error "define NTP_TRANSPORT before including host.bpf.h"
#endif

/* A record under construction and what the BPF side keeps beside it. */
struct host_entry {
  u64 rq;    /* struct request * owning the slot; 0 = free */
  u64 queue; /* the transport's queue */
  struct ntp_host_rec rec; /* emitted as is */
};

/* Records by struct request (common.bpf.h: direct-mapped tables). */
struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct host_entry);
} reqs SEC(".maps");

/* (queue, cid) -> struct request, for completions that carry only the cid. */
struct cid_slot {
  u64 queue;
  u64 rq;
  u16 cid;
  u16 pad[3];
};

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct cid_slot);
} cid_idx SEC(".maps");

/*
 * Exact per-queue occupancy over all requests (filter.track_queues, off by
 * default: it costs hash map operations on every request). A request is
 * counted when it enters ->queue_rq() after arming, so every decrement
 * matches an increment.
 */
struct qstat {
  s32 unsent;   /* command capsules not yet handed to the transport */
  s32 inflight; /* commands not yet completed */
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64); /* the transport's queue */
  __type(value, struct qstat);
} qstats SEC(".maps");

#define CNT_UNSENT 1
#define CNT_INFLIGHT 2

struct counted {
  u64 queue;
  u32 flags; /* CNT_* still to be decremented */
  u32 pad;
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 65536);
  __type(key, u64); /* struct request * */
  __type(value, struct counted);
} counted SEC(".maps");

/* ->queue_rq() -> nvme_setup_cmd, on this thread */
struct pending {
  u64 tid;
  u64 rq;
  u64 queue;
  u64 ts;
  u16 qid;
  u16 q_unsent;
  u16 q_inflight;
  u16 pad;
};
NTP_CPU_CTX(pend, struct pending);

/*
 * Receive context: set while the transport processes what the target sent,
 * so that the completion hooks know the request is this transport's.
 * `arrival` is when that data reached the host (0: not known).
 */
struct host_rx {
  u64 tid;
  u64 queue;
  u64 skb; /* TCP: the skb being read */
  u64 arrival;
};
NTP_CPU_CTX(rx_ctx, struct host_rx);

/* blk_mq_rq_from_pdu(): the driver PDU follows struct request. */
static __always_inline u64 rq_from_pdu(const void* pdu) {
  return ntp_scalar((u64)pdu - bpf_core_type_size(struct request));
}

/* The record of request @rq (a plain number, see ntp_scalar), or NULL. */
static __always_inline struct host_entry* host_entry(u64 rq) {
  u32 slot = ntp_slot(rq, NTP_REC_SLOTS);
  struct host_entry* e = bpf_map_lookup_elem(&reqs, &slot);

  return (e && e->rq == rq) ? e : NULL;
}

static __always_inline struct ntp_host_rec* host_rec(u64 rq) {
  struct host_entry* e = host_entry(rq);

  return e ? &e->rec : NULL;
}

static __always_inline u32 cid_slot_of(u64 queue, u16 cid) {
  return ntp_slot(queue ^ ((u64)cid << 6), NTP_REC_SLOTS);
}

static __always_inline struct ntp_host_rec* host_rec_by_cid(u64 queue, u16 cid) {
  u32 slot = cid_slot_of(queue, cid);
  struct cid_slot* s = bpf_map_lookup_elem(&cid_idx, &slot);

  return (s && s->queue == queue && s->cid == cid) ? host_rec(s->rq) : NULL;
}

static __always_inline struct qstat* get_qstat(u64 queue) {
  struct qstat* qs = bpf_map_lookup_elem(&qstats, &queue);

  if (!qs) {
    struct qstat zero = {};

    bpf_map_update_elem(&qstats, &queue, &zero, BPF_NOEXIST);
    qs = bpf_map_lookup_elem(&qstats, &queue);
  }
  return qs;
}

/* Drop @bits of a counted request from its queue's occupancy. */
static __always_inline void uncount(u64 rq, u32 bits, bool forget) {
  struct counted* c = bpf_map_lookup_elem(&counted, &rq);
  struct qstat* qs;
  u32 drop;

  if (!c) return;
  drop = c->flags & bits;
  c->flags &= ~bits;
  qs = bpf_map_lookup_elem(&qstats, &c->queue);
  if (qs) {
    if (drop & CNT_UNSENT) __sync_fetch_and_add(&qs->unsent, -1);
    if (drop & CNT_INFLIGHT) __sync_fetch_and_add(&qs->inflight, -1);
  }
  if (forget) bpf_map_delete_elem(&counted, &rq);
}

static __always_inline void host_rx_enter(u64 queue, u64 skb, u64 arrival) {
  u32 zero = 0;
  struct host_rx* c = bpf_map_lookup_elem(&rx_ctx, &zero);

  if (!c) return;
  c->tid = ntp_tid();
  c->queue = queue;
  c->skb = skb;
  c->arrival = arrival;
}

static __always_inline int host_rx_exit(void) {
  struct host_rx* c = NTP_CTX_GET(rx_ctx);

  if (c) c->tid = 0;
  return 0;
}

/* ---- submission ---- */

/* The transport's ->queue_rq(): hctx i drives the transport's queue i + 1. */
static __always_inline int host_queue_rq(struct blk_mq_hw_ctx* hctx,
                                         const struct blk_mq_queue_data* bd) {
  u32 zero = 0;
  struct pending* p = bpf_map_lookup_elem(&pend, &zero);

  if (!p || !ntp_armed) return 0;
  p->tid = ntp_tid();
  p->ts = bpf_ktime_get_ns();
  p->rq = (u64)BPF_CORE_READ(bd, rq);
  p->queue = (u64)BPF_CORE_READ(hctx, driver_data);
  p->qid = BPF_CORE_READ(hctx, queue_num) + 1;
  p->q_unsent = p->q_inflight = 0;

  if (filter.track_queues) {
    struct counted c = {.queue = p->queue, .flags = CNT_UNSENT | CNT_INFLIGHT};
    struct qstat* qs = get_qstat(p->queue);

    /* a request re-dispatched after BUSY is still counted: no double count */
    if (qs && !bpf_map_lookup_elem(&counted, &p->rq)) {
      p->q_unsent = ntp_clamp_u16(qs->unsent);
      p->q_inflight = ntp_clamp_u16(qs->inflight);
      bpf_map_update_elem(&counted, &p->rq, &c, BPF_ANY);
      __sync_fetch_and_add(&qs->unsent, 1);
      __sync_fetch_and_add(&qs->inflight, 1);
    }
  }
  return 0;
}

/* nvme_setup_cmd() runs inside ->queue_rq(): the cid is known, sample here. */
SEC("raw_tp/nvme_setup_cmd")
int BPF_PROG(ntp_h_setup_cmd, struct request* rq, void* cmd) {
  struct pending* p = NTP_CTX_GET(pend);
  struct ntp_cmd_id id = {};
  struct ntp_host_rec* rec;
  struct host_entry* e;
  struct nvme_request* nreq;
  struct nvme_ctrl* ctrl;
  struct cid_slot* cs;
  u32 bytes, instance, slot;
  u64 key = (u64)rq;

  /* Only requests that came through this transport's ->queue_rq(). */
  if (!p || p->rq != key) return 0;
  p->tid = 0;

  if (ntp_read_cmd(cmd, &id) || !ntp_is_rw(id.opcode)) return 0;
  /* throughput over all commands, counted as they are submitted */
  bytes = BPF_CORE_READ(rq, __data_len);
  if (id.opcode == NTP_NVME_CMD_WRITE) {
    ntp_count(NTP_CNT_HOST_WRITE_IOS, 1);
    ntp_count(NTP_CNT_HOST_WRITE_BYTES, bytes);
  } else {
    ntp_count(NTP_CNT_HOST_READ_IOS, 1);
    ntp_count(NTP_CNT_HOST_READ_BYTES, bytes);
  }
  if (filter.io_type != 2 && filter.io_type != (id.opcode == NTP_NVME_CMD_WRITE))
    return 0;
  if (!ntp_sampled(id.cid, id.slba, id.nlb)) return 0;
  if (!ntp_size_matches(bytes)) return 0;
  nreq = (void*)rq + bpf_core_type_size(struct request);
  ctrl = BPF_CORE_READ(nreq, ctrl);
  instance = BPF_CORE_READ(ctrl, instance);
  if (filter.ctrl_filter &&
      (instance > 255 ||
       !(filter.ctrl_mask[(instance >> 6) & 3] & (1ULL << (instance & 63)))))
    return 0;
  if (filter.nsid && id.nsid != filter.nsid) return 0;

  slot = ntp_slot(key, NTP_REC_SLOTS);
  e = bpf_map_lookup_elem(&reqs, &slot);
  if (!e) return 0;
  /* the slot's owner never completed (error path) or collides with us */
  if (e->rq)
    ntp_count(NTP_CNT_STALE, 1);
  else
    ntp_inflight_add(1);
  e->rq = 0;
  __builtin_memset(&e->rec, 0, sizeof(e->rec));
  e->queue = p->queue;
  rec = &e->rec;
  rec->id = id;
  rec->bytes = bytes;
  rec->ctrl_instance = instance;
  rec->hdr.kind = NTP_REC_HOST;
  rec->hdr.transport = NTP_TRANSPORT;
  if (id.opcode == NTP_NVME_CMD_WRITE) rec->hdr.flags |= NTP_F_WRITE;
  if (filter.track_queues) rec->hdr.flags |= NTP_F_QUEUES;
  rec->id.cntlid = BPF_CORE_READ(ctrl, cntlid);
  rec->id.qid = p->qid;
  rec->q_unsent = p->q_unsent;
  rec->q_inflight = p->q_inflight;
  BPF_CORE_READ_STR_INTO(&rec->disk, rq, q, disk, disk_name);
  rec->ts[NTP_H_START] = BPF_CORE_READ(rq, start_time_ns);
  rec->ts[NTP_H_QUEUE_RQ] = p->ts;
  e->rq = key; /* last: the slot is ours from here on */

  slot = cid_slot_of(p->queue, id.cid);
  cs = bpf_map_lookup_elem(&cid_idx, &slot);
  if (cs) {
    cs->queue = p->queue;
    cs->cid = id.cid;
    cs->rq = key;
  }
  ntp_count(NTP_CNT_HOST_SAMPLED, 1);
  return 0;
}

/* ---- completion ---- */

/*
 * nvme_try_complete_req() -> blk_mq_complete_request_remote(): the completion
 * has been parsed. Other drivers call this too; the receive context
 * restricts it to this transport.
 */
static __always_inline struct ntp_host_rec* host_complete(struct request* rq) {
  struct host_rx* c;
  struct ntp_host_rec* rec;
  u64 key;

  if (ntp_idle()) return NULL;
  c = NTP_CTX_GET(rx_ctx);
  if (!c) return NULL;
  key = ntp_scalar((u64)rq);
  if (filter.track_queues) uncount(key, CNT_UNSENT | CNT_INFLIGHT, true);

  rec = host_rec(key);
  if (!rec) return NULL;
  if (c->arrival) rec->ts[NTP_H_RSP_ARR] = c->arrival;
  rec->ts[NTP_H_RSP_PROC] = bpf_ktime_get_ns();
  return rec;
}

SEC("raw_tp/nvme_complete_rq")
int BPF_PROG(ntp_h_nvme_done, struct request* rq) {
  struct ntp_host_rec* rec;

  if (!ntp_inflight) return 0;
  rec = host_rec((u64)rq);
  if (rec) ntp_stamp_once(&rec->ts[NTP_H_NVME_DONE], bpf_ktime_get_ns());
  return 0;
}

SEC("raw_tp/block_rq_complete")
int BPF_PROG(ntp_h_blk_done, struct request* rq) {
  struct host_entry* e;
  struct ntp_host_rec* rec;
  struct cid_slot* cs;
  u32 slot;

  if (!ntp_inflight) return 0;
  e = host_entry((u64)rq);
  if (!e) return 0;
  rec = &e->rec;
  rec->ts[NTP_H_BLK_DONE] = bpf_ktime_get_ns();
  /* over TCP the host sees whether the data needed an R2T */
  if (NTP_TRANSPORT == NTP_TR_TCP && (rec->hdr.flags & NTP_F_WRITE) &&
      !(rec->hdr.flags & NTP_F_R2T))
    rec->hdr.flags |= NTP_F_INLINE_DATA;
  rec->hdr.cpu = bpf_get_smp_processor_id();

  if (bpf_ringbuf_output(&events, rec, sizeof(*rec), 0))
    ntp_count(NTP_CNT_RB_DROPS, 1);
  else
    ntp_count(NTP_CNT_HOST_EMITTED, 1);

  slot = cid_slot_of(e->queue, rec->id.cid);
  cs = bpf_map_lookup_elem(&cid_idx, &slot);
  if (cs && cs->rq == e->rq) cs->rq = 0;
  e->rq = 0;
  ntp_inflight_add(-1);
  return 0;
}

#endif /* NTPROF_HOST_BPF_H */
