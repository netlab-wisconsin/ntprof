// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP initiator side (drivers/nvme/host/tcp.c, unmodified).
 *
 * One record per sampled request, keyed by its struct request, from blk-mq
 * start to block_rq_complete. Hooks (see ebpf/DESIGN.md for the mapping to
 * v1's tracepoints):
 *
 *   kprobe nvme_tcp_queue_rq          -> NTP_H_QUEUE_RQ, queue, qid
 *   raw_tp nvme_setup_cmd             -> cid, opcode, LBA: sampling decision
 *   kretprobe nvme_tcp_try_send_*     -> NTP_H_CMD_SENT / NTP_H_DATA_SENT
 *   kprobe nvme_tcp_try_send_data_pdu -> NTP_H_DATA_START
 *   k(ret)probe nvme_tcp_recv_skb     -> receive context (queue, skb arrival)
 *   kprobe skb_copy_datagram_iter     -> NTP_H_C2H_* (first data copy of a C2H PDU)
 *   kprobe nvme_tcp_setup_h2c_data_pdu-> NTP_H_R2T_* (when called from the receive path)
 *   kprobe blk_mq_complete_request_remote -> NTP_H_RSP_* (CQE parsed)
 *   raw_tp nvme_complete_rq           -> NTP_H_NVME_DONE
 *   raw_tp block_rq_complete          -> NTP_H_BLK_DONE, record emitted
 */
#include "common.bpf.h"

/* queue_rq -> setup_cmd hand-over, per thread */
struct pending {
  u64 rq;
  u64 queue;
  u64 ts;
  u16 qid;
  u16 q_unsent;
  u16 q_inflight;
  u16 pad;
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64); /* pid_tgid */
  __type(value, struct pending);
} pend SEC(".maps");

/* Records under construction, by struct request address. */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 32768);
  __type(key, u64);
  __type(value, struct ntp_host_rec);
} reqs SEC(".maps");

/* (queue, cid) -> struct request, for PDUs that carry only the cid. */
struct qcid {
  u64 queue;
  u16 cid;
  u16 pad[3];
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 32768);
  __type(key, struct qcid);
  __type(value, u64);
} cid_idx SEC(".maps");

/*
 * Per-queue occupancy over all requests (sampled or not). A request is
 * counted in `counted` when it enters queue_rq after arming, so every
 * decrement matches an increment.
 */
struct qstat {
  s32 unsent;   /* command capsules not yet handed to the socket */
  s32 inflight; /* commands not yet completed */
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64); /* struct nvme_tcp_queue * */
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
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 65536);
  __type(key, u64); /* struct request * */
  __type(value, struct counted);
} counted SEC(".maps");

/* nvme_tcp_request being sent by this thread (kprobe -> kretprobe). */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64);
  __type(value, u64);
} send_ctx SEC(".maps");

/* Receive context: set while nvme_tcp_recv_skb() runs on this thread. */
struct rx_ctx {
  u64 queue;
  u64 skb;
  u64 arrival;
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64);
  __type(value, struct rx_ctx);
} rx_ctx SEC(".maps");

/* Scratch space for building a record (too large for the BPF stack). */
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, struct ntp_host_rec);
} scratch SEC(".maps");

static __always_inline struct qstat* get_qstat(u64 queue) {
  struct qstat* qs = bpf_map_lookup_elem(&qstats, &queue);

  if (!qs) {
    struct qstat zero = {};

    bpf_map_update_elem(&qstats, &queue, &zero, BPF_NOEXIST);
    qs = bpf_map_lookup_elem(&qstats, &queue);
  }
  return qs;
}

static __always_inline u16 clamp_u16(s32 v) {
  return v < 0 ? 0 : v > 0xffff ? 0xffff : (u16)v;
}

/* blk_mq_rq_from_pdu(): the driver PDU follows struct request. */
static __always_inline u64 rq_from_pdu(const void* pdu) {
  return (u64)pdu - bpf_core_type_size(struct request);
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

static __always_inline bool size_matches(u32 bytes) {
  if (filter.n_sizes) {
#pragma unroll
    for (int i = 0; i < NTP_MAX_SIZE_FILTERS; i++)
      if (i < filter.n_sizes && filter.sizes[i] == bytes) return true;
    return false;
  }
  if (filter.min_bytes && bytes < filter.min_bytes) return false;
  if (filter.max_bytes && bytes > filter.max_bytes) return false;
  return true;
}

SEC("kprobe/nvme_tcp_queue_rq")
int BPF_KPROBE(ntp_h_queue_rq, struct blk_mq_hw_ctx* hctx,
               const struct blk_mq_queue_data* bd) {
  struct pending p = {};
  u64 tid = ntp_tid();

  if (!ntp_armed) return 0;
  p.ts = bpf_ktime_get_ns();
  p.rq = (u64)BPF_CORE_READ(bd, rq);
  p.queue = (u64)BPF_CORE_READ(hctx, driver_data);
  /* nvme_tcp_init_hctx(): hctx i drives ctrl->queues[i + 1] */
  p.qid = BPF_CORE_READ(hctx, queue_num) + 1;

  if (filter.track_queues) {
    struct counted c = {.queue = p.queue, .flags = CNT_UNSENT | CNT_INFLIGHT};
    struct qstat* qs = get_qstat(p.queue);

    /* a request re-dispatched after BUSY is still counted: no double count */
    if (qs && !bpf_map_lookup_elem(&counted, &p.rq)) {
      p.q_unsent = clamp_u16(qs->unsent);
      p.q_inflight = clamp_u16(qs->inflight);
      bpf_map_update_elem(&counted, &p.rq, &c, BPF_ANY);
      __sync_fetch_and_add(&qs->unsent, 1);
      __sync_fetch_and_add(&qs->inflight, 1);
    }
  }
  bpf_map_update_elem(&pend, &tid, &p, BPF_ANY);
  return 0;
}

SEC("raw_tp/nvme_setup_cmd")
int BPF_PROG(ntp_h_setup_cmd, struct request* rq, void* cmd) {
  u64 tid = ntp_tid();
  struct pending* p = bpf_map_lookup_elem(&pend, &tid);
  struct ntp_host_rec* rec;
  struct nvme_request* nreq;
  struct nvme_ctrl* ctrl;
  struct qcid qc = {};
  u32 zero = 0;
  u64 key;

  /* Only requests that came through nvme_tcp_queue_rq on this thread. */
  if (!p || p->rq != (u64)rq) return 0;

  rec = bpf_map_lookup_elem(&scratch, &zero);
  if (!rec) goto out;
  __builtin_memset(rec, 0, sizeof(*rec));

  if (ntp_read_cmd(cmd, &rec->id) || !ntp_is_rw(rec->id.opcode)) goto out;
  if (filter.io_type != 2 &&
      filter.io_type != (rec->id.opcode == NTP_NVME_CMD_WRITE))
    goto out;
  rec->bytes = BPF_CORE_READ(rq, __data_len);
  if (!size_matches(rec->bytes)) goto out;

  nreq = (void*)rq + bpf_core_type_size(struct request);
  ctrl = BPF_CORE_READ(nreq, ctrl);
  rec->ctrl_instance = BPF_CORE_READ(ctrl, instance);
  if (filter.ctrl_filter &&
      (rec->ctrl_instance > 255 ||
       !(filter.ctrl_mask[(rec->ctrl_instance >> 6) & 3] &
         (1ULL << (rec->ctrl_instance & 63)))))
    goto out;
  if (filter.nsid && rec->id.nsid != filter.nsid) goto out;
  if (!ntp_sampled(rec->id.cid, rec->id.slba, rec->id.nlb)) goto out;

  rec->hdr.kind = NTP_REC_HOST;
  rec->hdr.transport = NTP_TR_TCP;
  if (rec->id.opcode == NTP_NVME_CMD_WRITE) rec->hdr.flags |= NTP_F_WRITE;
  rec->id.cntlid = BPF_CORE_READ(ctrl, cntlid);
  rec->id.qid = p->qid;
  rec->q_unsent = p->q_unsent;
  rec->q_inflight = p->q_inflight;
  BPF_CORE_READ_STR_INTO(&rec->disk, rq, q, disk, disk_name);
  rec->ts[NTP_H_START] = BPF_CORE_READ(rq, start_time_ns);
  rec->ts[NTP_H_QUEUE_RQ] = p->ts;

  key = (u64)rq;
  /* An unfinished record on this request belonged to a command that ended
   * without block_rq_complete (error path): replace it. */
  if (bpf_map_lookup_elem(&reqs, &key)) ntp_count(NTP_CNT_STALE, 1);
  if (bpf_map_update_elem(&reqs, &key, rec, BPF_ANY)) {
    ntp_count(NTP_CNT_MAP_FULL, 1);
    goto out;
  }
  qc.queue = p->queue;
  qc.cid = rec->id.cid;
  bpf_map_update_elem(&cid_idx, &qc, &key, BPF_ANY);
  ntp_count(NTP_CNT_HOST_SAMPLED, 1);
out:
  bpf_map_delete_elem(&pend, &tid);
  return 0;
}

/* ---- send path ---- */

static __always_inline int send_enter(const struct nvme_tcp_request* req) {
  u64 tid = ntp_tid();
  u64 r = (u64)req;

  bpf_map_update_elem(&send_ctx, &tid, &r, BPF_ANY);
  return 0;
}

/*
 * A try_send function returned. 1 means the PDU (or data) it was sending went
 * out completely; nvme_tcp_done_send_req() then cleared queue->request if the
 * whole request was sent. send_mutex is still held, so queue->request cannot
 * have been refilled yet.
 */
static __always_inline int send_exit(int ret, bool cmd_pdu) {
  u64 tid = ntp_tid();
  u64* r = bpf_map_lookup_elem(&send_ctx, &tid);
  struct nvme_tcp_request* req;
  struct nvme_tcp_queue* queue;
  struct ntp_host_rec* rec;
  u64 rq, now;

  if (!r) return 0;
  req = (void*)*r;
  bpf_map_delete_elem(&send_ctx, &tid);
  if (ret != 1) return 0;

  rq = rq_from_pdu(req);
  if (cmd_pdu && filter.track_queues) uncount(rq, CNT_UNSENT, false);

  queue = BPF_CORE_READ(req, queue);
  if (BPF_CORE_READ(queue, request)) return 0; /* more of this request to send */

  rec = bpf_map_lookup_elem(&reqs, &rq);
  if (!rec) return 0;
  now = bpf_ktime_get_ns();
  if (!rec->ts[NTP_H_CMD_SENT])
    rec->ts[NTP_H_CMD_SENT] = now;
  else if (rec->ts[NTP_H_R2T_PROC])
    rec->ts[NTP_H_DATA_SENT] = now; /* the last solicited data PDU */
  return 0;
}

/* The try_send functions return int: only the low 32 bits of ax are defined. */
SEC("kprobe/nvme_tcp_try_send_cmd_pdu")
int BPF_KPROBE(ntp_h_send_cmd, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_cmd_pdu")
int BPF_KRETPROBE(ntp_h_send_cmd_ret, int ret) { return send_exit(ret, true); }

SEC("kprobe/nvme_tcp_try_send_data")
int BPF_KPROBE(ntp_h_send_data, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_data")
int BPF_KRETPROBE(ntp_h_send_data_ret, int ret) { return send_exit(ret, false); }

SEC("kprobe/nvme_tcp_try_send_ddgst")
int BPF_KPROBE(ntp_h_send_ddgst, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_ddgst")
int BPF_KRETPROBE(ntp_h_send_ddgst_ret, int ret) {
  return send_exit(ret, false);
}

/* The H2C data PDU header of an R2T: the host starts sending the data. */
SEC("kprobe/nvme_tcp_try_send_data_pdu")
int BPF_KPROBE(ntp_h_send_h2c, struct nvme_tcp_request* req) {
  u64 rq = rq_from_pdu(req);
  struct ntp_host_rec* rec = bpf_map_lookup_elem(&reqs, &rq);

  if (rec && rec->ts[NTP_H_R2T_PROC])
    ntp_stamp_once(&rec->ts[NTP_H_DATA_START], bpf_ktime_get_ns());
  return 0;
}

/* ---- receive path ---- */

SEC("kprobe/nvme_tcp_recv_skb")
int BPF_KPROBE(ntp_h_recv_skb, void* desc, struct sk_buff* skb) {
  u64 tid = ntp_tid();
  u64 now = bpf_ktime_get_ns();
  struct rx_ctx c = {};

  /* nvme_tcp_setup_sock_ops() stores the queue in sk_user_data */
  c.queue = (u64)BPF_CORE_READ(skb, sk, sk_user_data);
  c.skb = (u64)skb;
  c.arrival = ntp_skb_arrival(skb, now);
  bpf_map_update_elem(&rx_ctx, &tid, &c, BPF_ANY);
  return 0;
}

SEC("kretprobe/nvme_tcp_recv_skb")
int BPF_KRETPROBE(ntp_h_recv_skb_ret) {
  u64 tid = ntp_tid();

  bpf_map_delete_elem(&rx_ctx, &tid);
  return 0;
}

static __always_inline struct ntp_host_rec* rec_by_cid(u64 queue, u16 cid) {
  struct qcid qc = {.queue = queue, .cid = cid};
  u64* rq = bpf_map_lookup_elem(&cid_idx, &qc);

  return rq ? bpf_map_lookup_elem(&reqs, rq) : NULL;
}

/* First data copy of a C2H data PDU: the PDU header in queue->pdu names it. */
static __always_inline int c2h_copy(const struct sk_buff* skb) {
  u64 tid = ntp_tid();
  struct rx_ctx* c = bpf_map_lookup_elem(&rx_ctx, &tid);
  struct ntp_tcp_data_pdu pdu;
  struct nvme_tcp_queue* queue;
  struct ntp_host_rec* rec;
  void* hdr;

  if (!c || c->skb != (u64)skb) return 0;
  queue = (void*)c->queue;
  hdr = BPF_CORE_READ(queue, pdu);
  if (bpf_probe_read_kernel(&pdu, sizeof(pdu), hdr)) return 0;
  if (pdu.hdr.type != nvme_tcp_c2h_data) return 0;

  rec = rec_by_cid(c->queue, pdu.command_id);
  if (!rec || rec->ts[NTP_H_C2H_PROC]) return 0;
  rec->ts[NTP_H_C2H_ARR] = c->arrival;
  rec->ts[NTP_H_C2H_PROC] = bpf_ktime_get_ns();
  return 0;
}

SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_h_copy, const struct sk_buff* skb) { return c2h_copy(skb); }

SEC("kprobe/skb_copy_and_hash_datagram_iter")
int BPF_KPROBE(ntp_h_copy_hash, const struct sk_buff* skb) {
  return c2h_copy(skb);
}

/* Called by nvme_tcp_handle_r2t() (receive path) and by the send path. */
SEC("kprobe/nvme_tcp_setup_h2c_data_pdu")
int BPF_KPROBE(ntp_h_r2t, struct nvme_tcp_request* req) {
  u64 tid = ntp_tid();
  struct rx_ctx* c = bpf_map_lookup_elem(&rx_ctx, &tid);
  struct ntp_host_rec* rec;
  u64 rq;

  if (!c) return 0;
  rq = rq_from_pdu(req);
  rec = bpf_map_lookup_elem(&reqs, &rq);
  if (!rec) return 0;
  if (rec->ts[NTP_H_R2T_PROC]) {
    rec->hdr.flags |= NTP_F_MULTI_R2T;
    return 0;
  }
  rec->hdr.flags |= NTP_F_R2T;
  rec->ts[NTP_H_R2T_ARR] = c->arrival;
  rec->ts[NTP_H_R2T_PROC] = bpf_ktime_get_ns();
  return 0;
}

/*
 * nvme_try_complete_req() -> blk_mq_complete_request_remote(): the CQE (or
 * the last C2H PDU with the SUCCESS flag) has been parsed. Other drivers call
 * this too; the receive context restricts it to nvme-tcp.
 */
SEC("kprobe/blk_mq_complete_request_remote")
int BPF_KPROBE(ntp_h_complete, struct request* rq) {
  u64 tid = ntp_tid();
  struct rx_ctx* c = bpf_map_lookup_elem(&rx_ctx, &tid);
  struct ntp_host_rec* rec;
  u64 key = (u64)rq;
  u32 bytes;

  if (!c) return 0;
  if (filter.track_queues) uncount(key, CNT_UNSENT | CNT_INFLIGHT, true);

  bytes = BPF_CORE_READ(rq, __data_len);
  if (BPF_CORE_READ(rq, cmd_flags) & 1) { /* REQ_OP_WRITE */
    ntp_count(NTP_CNT_HOST_WRITE_IOS, 1);
    ntp_count(NTP_CNT_HOST_WRITE_BYTES, bytes);
  } else {
    ntp_count(NTP_CNT_HOST_READ_IOS, 1);
    ntp_count(NTP_CNT_HOST_READ_BYTES, bytes);
  }

  rec = bpf_map_lookup_elem(&reqs, &key);
  if (!rec) return 0;
  rec->ts[NTP_H_RSP_ARR] = c->arrival;
  rec->ts[NTP_H_RSP_PROC] = bpf_ktime_get_ns();
  if (rec->ts[NTP_H_C2H_PROC]) {
    struct nvme_tcp_queue* queue = (void*)c->queue;
    void* hdr = BPF_CORE_READ(queue, pdu);
    u8 type = 0;

    bpf_probe_read_kernel(&type, sizeof(type), hdr);
    if (type == nvme_tcp_c2h_data) rec->hdr.flags |= NTP_F_DATA_SUCCESS;
  }
  return 0;
}

SEC("raw_tp/nvme_complete_rq")
int BPF_PROG(ntp_h_nvme_done, struct request* rq) {
  u64 key = (u64)rq;
  struct ntp_host_rec* rec = bpf_map_lookup_elem(&reqs, &key);

  if (rec) ntp_stamp_once(&rec->ts[NTP_H_NVME_DONE], bpf_ktime_get_ns());
  return 0;
}

SEC("raw_tp/block_rq_complete")
int BPF_PROG(ntp_h_blk_done, struct request* rq) {
  u64 key = (u64)rq;
  struct ntp_host_rec* rec = bpf_map_lookup_elem(&reqs, &key);
  struct nvme_tcp_request* req;
  struct qcid qc = {};

  if (!rec) return 0;
  rec->ts[NTP_H_BLK_DONE] = bpf_ktime_get_ns();
  if ((rec->hdr.flags & NTP_F_WRITE) && !(rec->hdr.flags & NTP_F_R2T))
    rec->hdr.flags |= NTP_F_INLINE_DATA;
  rec->hdr.cpu = bpf_get_smp_processor_id();

  if (bpf_ringbuf_output(&events, rec, sizeof(*rec), 0))
    ntp_count(NTP_CNT_RB_DROPS, 1);
  else
    ntp_count(NTP_CNT_HOST_EMITTED, 1);

  req = (void*)rq + bpf_core_type_size(struct request);
  qc.queue = (u64)BPF_CORE_READ(req, queue);
  qc.cid = rec->id.cid;
  bpf_map_delete_elem(&cid_idx, &qc);
  bpf_map_delete_elem(&reqs, &key);
  return 0;
}
