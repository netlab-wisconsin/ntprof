// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP target side (drivers/nvme/target/tcp.c, unmodified).
 *
 * One record per sampled command, keyed by (submission queue, cid), from the
 * arrival of the command capsule to the moment its response capsule is handed
 * to the socket. Hooks:
 *
 *   k(ret)probe nvmet_tcp_io_work     -> context: which queue this thread serves
 *   kprobe skb_copy_datagram_iter     -> arrival time of the skb being read
 *   raw_tp nvmet_req_init             -> NTP_T_CMD_ARR / NTP_T_CMD_PROC, sampling
 *   kprobe nvmet_tcp_queue_response   -> NTP_T_R2T_QUEUED / NTP_T_RSP_QUEUED
 *   kprobe nvmet_tcp_build_pdu_iovec  -> NTP_T_DATA_* (solicited H2C data)
 *   kprobe nvmet_{bdev,file}_execute_rw, nvmet_passthru_execute_cmd -> NTP_T_EXEC
 *   raw_tp block_rq_issue / _complete -> NTP_T_BE_* (bdev backend)
 *   raw_tp nvmet_req_complete         -> NTP_T_COMPLETE
 *   kprobe sock_sendmsg               -> NTP_T_R2T_SENT / C2H_SENT / RSP_SENT
 */
#include "common.bpf.h"

struct tkey {
  u64 sq; /* struct nvmet_sq * */
  u16 cid;
  u16 pad[3];
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 32768);
  __type(key, struct tkey);
  __type(value, struct ntp_tgt_rec);
} treqs SEC(".maps");

/* struct nvmet_req * of sampled commands, to recognise them in bio->bi_private */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 32768);
  __type(key, u64);
  __type(value, struct tkey);
} treq_ptr SEC(".maps");

/*
 * Per-queue occupancy over all commands. A command is counted in `tcounted`
 * when nvmet_req_init() sees it after arming, so every decrement matches an
 * increment. The nvmet_req address identifies the command slot.
 */
struct tqstat {
  s32 outstanding; /* commands received, response not yet sent */
  s32 resp_queue;  /* completions queued, response not yet sent */
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64); /* struct nvmet_sq * */
  __type(value, struct tqstat);
} tqstats SEC(".maps");

#define CNT_OUTSTANDING 1
#define CNT_RESP_QUEUED 2

struct tcounted {
  u64 sq;
  u32 flags; /* CNT_* still to be decremented */
  u32 pad;
};

struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 65536);
  __type(key, u64); /* struct nvmet_req * */
  __type(value, struct tcounted);
} tcounted SEC(".maps");

/* Set while nvmet_tcp_io_work() runs on this thread. */
struct io_ctx {
  u64 queue;
  u64 sk;
  u64 last_arrival; /* arrival of the last skb read on this queue */
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, u64);
  __type(value, struct io_ctx);
} io_ctx SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, struct ntp_tgt_rec);
} scratch SEC(".maps");

static __always_inline struct tqstat* get_tqstat(u64 sq) {
  struct tqstat* qs = bpf_map_lookup_elem(&tqstats, &sq);

  if (!qs) {
    struct tqstat zero = {};

    bpf_map_update_elem(&tqstats, &sq, &zero, BPF_NOEXIST);
    qs = bpf_map_lookup_elem(&tqstats, &sq);
  }
  return qs;
}

static __always_inline u16 clamp_u16(s32 v) {
  return v < 0 ? 0 : v > 0xffff ? 0xffff : (u16)v;
}

static __always_inline int req_key(const struct nvmet_req* req,
                                   struct tkey* k) {
  struct ntp_nvme_cmd* cmd = BPF_CORE_READ(req, cmd);

  k->sq = (u64)BPF_CORE_READ(req, sq);
  return bpf_probe_read_kernel(&k->cid, sizeof(k->cid), &cmd->command_id);
}

static __always_inline struct ntp_tgt_rec* rec_of(const struct nvmet_req* req) {
  struct tkey k = {};

  if (req_key(req, &k)) return NULL;
  return bpf_map_lookup_elem(&treqs, &k);
}

/* container_of() in both directions between nvmet_tcp_cmd and its nvmet_req */
static __always_inline struct nvmet_tcp_cmd* cmd_of(const struct nvmet_req* req) {
  return (void*)req - bpf_core_field_offset(struct nvmet_tcp_cmd, req);
}

static __always_inline struct nvmet_req* req_of(const struct nvmet_tcp_cmd* cmd) {
  return (void*)cmd + bpf_core_field_offset(struct nvmet_tcp_cmd, req);
}

/* Drop @bits of a counted command from its queue's occupancy. */
static __always_inline void uncount(u64 req, u32 bits, bool forget) {
  struct tcounted* c = bpf_map_lookup_elem(&tcounted, &req);
  struct tqstat* qs;
  u32 drop;

  if (!c) return;
  drop = c->flags & bits;
  c->flags &= ~bits;
  qs = bpf_map_lookup_elem(&tqstats, &c->sq);
  if (qs) {
    if (drop & CNT_OUTSTANDING) __sync_fetch_and_add(&qs->outstanding, -1);
    if (drop & CNT_RESP_QUEUED) __sync_fetch_and_add(&qs->resp_queue, -1);
  }
  if (forget) bpf_map_delete_elem(&tcounted, &req);
}

/* ---- context ---- */

SEC("kprobe/nvmet_tcp_io_work")
int BPF_KPROBE(ntp_t_io_work, struct work_struct* w) {
  struct nvmet_tcp_queue* queue =
      (void*)w - bpf_core_field_offset(struct nvmet_tcp_queue, io_work);
  struct io_ctx c = {};
  u64 tid = ntp_tid();

  c.queue = (u64)queue;
  c.sk = (u64)BPF_CORE_READ(queue, sock, sk);
  bpf_map_update_elem(&io_ctx, &tid, &c, BPF_ANY);
  return 0;
}

SEC("kretprobe/nvmet_tcp_io_work")
int BPF_KRETPROBE(ntp_t_io_work_ret) {
  u64 tid = ntp_tid();

  bpf_map_delete_elem(&io_ctx, &tid);
  return 0;
}

/* kernel_recvmsg() -> tcp_recvmsg() copies each skb with this helper. */
SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_t_copy, const struct sk_buff* skb) {
  u64 tid = ntp_tid();
  struct io_ctx* c = bpf_map_lookup_elem(&io_ctx, &tid);

  if (!c || (u64)BPF_CORE_READ(skb, sk) != c->sk) return 0;
  c->last_arrival = ntp_skb_arrival(skb, bpf_ktime_get_ns());
  return 0;
}

/* ---- command arrival ---- */

SEC("raw_tp/nvmet_req_init")
int BPF_PROG(ntp_t_req_init, struct nvmet_req* req, void* cmd) {
  u64 tid = ntp_tid();
  struct io_ctx* c = bpf_map_lookup_elem(&io_ctx, &tid);
  struct ntp_tgt_rec* rec;
  struct nvmet_sq* sq;
  struct tkey k = {};
  u64 now, rptr = (u64)req;
  u32 zero = 0;

  if (!c || !ntp_armed) return 0; /* not nvmet-tcp, or not armed yet */
  sq = BPF_CORE_READ(req, sq);
  if (!BPF_CORE_READ(sq, qid)) return 0; /* admin queue */

  rec = bpf_map_lookup_elem(&scratch, &zero);
  if (!rec) return 0;
  __builtin_memset(rec, 0, sizeof(*rec));
  if (filter.track_queues) {
    struct tcounted tc = {.sq = (u64)sq, .flags = CNT_OUTSTANDING};
    struct tqstat* qs = get_tqstat((u64)sq);

    /* the slot's previous command, if its response was never seen */
    uncount(rptr, CNT_OUTSTANDING | CNT_RESP_QUEUED, true);
    if (qs) {
      rec->q_outstanding = clamp_u16(qs->outstanding);
      rec->q_resp_queue = clamp_u16(qs->resp_queue);
      bpf_map_update_elem(&tcounted, &rptr, &tc, BPF_ANY);
      __sync_fetch_and_add(&qs->outstanding, 1);
    }
  }
  ntp_count(NTP_CNT_TGT_CMDS, 1);

  if (ntp_read_cmd(cmd, &rec->id) || !ntp_is_rw(rec->id.opcode)) return 0;
  if (filter.io_type != 2 &&
      filter.io_type != (rec->id.opcode == NTP_NVME_CMD_WRITE))
    return 0;
  if (!ntp_sampled(rec->id.cid, rec->id.slba, rec->id.nlb)) return 0;

  now = bpf_ktime_get_ns();
  rec->hdr.kind = NTP_REC_TARGET;
  rec->hdr.transport = NTP_TR_TCP;
  if (rec->id.opcode == NTP_NVME_CMD_WRITE) rec->hdr.flags |= NTP_F_WRITE;
  rec->id.cntlid = BPF_CORE_READ(sq, ctrl, cntlid);
  rec->id.qid = BPF_CORE_READ(sq, qid);
  rec->ts[NTP_T_CMD_ARR] = c->last_arrival ? c->last_arrival : now;
  rec->ts[NTP_T_CMD_PROC] = now;

  k.sq = (u64)sq;
  k.cid = rec->id.cid;
  if (bpf_map_lookup_elem(&treqs, &k)) ntp_count(NTP_CNT_STALE, 1);
  if (bpf_map_update_elem(&treqs, &k, rec, BPF_ANY)) {
    ntp_count(NTP_CNT_MAP_FULL, 1);
    return 0;
  }
  bpf_map_update_elem(&treq_ptr, &rptr, &k, BPF_ANY);
  ntp_count(NTP_CNT_TGT_SAMPLED, 1);
  return 0;
}

/*
 * ->queue_response() is used twice for a write without in-capsule data:
 * first to send the R2T (data still to come), then for the completion.
 */
SEC("kprobe/nvmet_tcp_queue_response")
int BPF_KPROBE(ntp_t_queue_resp, struct nvmet_req* req) {
  struct nvmet_tcp_cmd* cmd = cmd_of(req);
  struct ntp_tgt_rec* rec = rec_of(req);
  struct ntp_nvme_cmd* nc = BPF_CORE_READ(req, cmd);
  u64 now = bpf_ktime_get_ns();
  u8 opcode = 0;
  bool r2t;

  bpf_probe_read_kernel(&opcode, sizeof(opcode), &nc->opcode);
  /* nvmet_tcp_need_data_in(): a write whose data has not all arrived */
  r2t = opcode == NTP_NVME_CMD_WRITE &&
        BPF_CORE_READ(cmd, rbytes_done) < BPF_CORE_READ(req, transfer_len);
  if (r2t) {
    if (rec) ntp_stamp_once(&rec->ts[NTP_T_R2T_QUEUED], now);
    return 0;
  }
  if (filter.track_queues) {
    u64 rptr = (u64)req;
    struct tcounted* tc = bpf_map_lookup_elem(&tcounted, &rptr);

    if (tc && !(tc->flags & CNT_RESP_QUEUED)) {
      struct tqstat* qs = bpf_map_lookup_elem(&tqstats, &tc->sq);

      tc->flags |= CNT_RESP_QUEUED;
      if (qs) __sync_fetch_and_add(&qs->resp_queue, 1);
    }
  }
  if (rec) ntp_stamp_once(&rec->ts[NTP_T_RSP_QUEUED], now);
  return 0;
}

/* Solicited data: called once per H2C data PDU (and for in-capsule data). */
SEC("kprobe/nvmet_tcp_build_pdu_iovec")
int BPF_KPROBE(ntp_t_h2c, struct nvmet_tcp_cmd* cmd) {
  struct ntp_tgt_rec* rec = rec_of(req_of(cmd));
  u64 tid = ntp_tid();
  struct io_ctx* c;
  u64 now;

  if (!rec) return 0;
  if (!rec->ts[NTP_T_R2T_SENT]) {
    rec->hdr.flags |= NTP_F_INLINE_DATA;
    return 0;
  }
  if (rec->ts[NTP_T_DATA_PROC]) return 0;
  now = bpf_ktime_get_ns();
  c = bpf_map_lookup_elem(&io_ctx, &tid);
  rec->hdr.flags |= NTP_F_R2T;
  rec->ts[NTP_T_DATA_ARR] = (c && c->last_arrival) ? c->last_arrival : now;
  rec->ts[NTP_T_DATA_PROC] = now;
  return 0;
}

/* ---- backend ---- */

static __always_inline int exec(const struct nvmet_req* req) {
  struct ntp_tgt_rec* rec = rec_of(req);

  if (!rec || rec->ts[NTP_T_EXEC]) return 0;
  rec->ts[NTP_T_EXEC] = bpf_ktime_get_ns();
  /* parsed after the nvmet_req_init tracepoint fired */
  rec->bytes = BPF_CORE_READ(req, transfer_len);
  return 0;
}

SEC("kprobe/nvmet_bdev_execute_rw")
int BPF_KPROBE(ntp_t_exec_bdev, struct nvmet_req* req) { return exec(req); }

SEC("kprobe/nvmet_file_execute_rw")
int BPF_KPROBE(ntp_t_exec_file, struct nvmet_req* req) { return exec(req); }

SEC("kprobe/nvmet_passthru_execute_cmd")
int BPF_KPROBE(ntp_t_exec_pt, struct nvmet_req* req) { return exec(req); }

/* The bdev backend sets bio->bi_private to the nvmet_req. */
static __always_inline struct ntp_tgt_rec* rec_of_rq(const struct request* rq) {
  u64 priv = (u64)BPF_CORE_READ(rq, bio, bi_private);
  struct tkey* k;

  if (!priv) return NULL;
  k = bpf_map_lookup_elem(&treq_ptr, &priv);
  return k ? bpf_map_lookup_elem(&treqs, k) : NULL;
}

SEC("raw_tp/block_rq_issue")
int BPF_PROG(ntp_t_be_issue, struct request* rq) {
  struct ntp_tgt_rec* rec = rec_of_rq(rq);

  if (rec) ntp_stamp_once(&rec->ts[NTP_T_BE_ISSUE], bpf_ktime_get_ns());
  return 0;
}

SEC("raw_tp/block_rq_complete")
int BPF_PROG(ntp_t_be_done, struct request* rq) {
  struct ntp_tgt_rec* rec = rec_of_rq(rq);

  if (rec) rec->ts[NTP_T_BE_DONE] = bpf_ktime_get_ns();
  return 0;
}

SEC("raw_tp/nvmet_req_complete")
int BPF_PROG(ntp_t_complete, struct nvmet_req* req) {
  struct ntp_tgt_rec* rec = rec_of(req);

  if (rec) ntp_stamp_once(&rec->ts[NTP_T_COMPLETE], bpf_ktime_get_ns());
  return 0;
}

/* ---- send path ---- */

/* The command's last PDU is going out: emit its record and forget it. */
static __always_inline void finish(struct nvmet_req* req,
                                   struct ntp_tgt_rec* rec) {
  u64 rptr = (u64)req;
  struct tkey k = {};

  if (filter.track_queues)
    uncount(rptr, CNT_OUTSTANDING | CNT_RESP_QUEUED, true);
  if (!rec) return;

  rec->hdr.cpu = bpf_get_smp_processor_id();
  if (bpf_ringbuf_output(&events, rec, sizeof(*rec), 0))
    ntp_count(NTP_CNT_RB_DROPS, 1);
  else
    ntp_count(NTP_CNT_TGT_EMITTED, 1);

  req_key(req, &k);
  bpf_map_delete_elem(&treq_ptr, &rptr);
  bpf_map_delete_elem(&treqs, &k);
}

/*
 * nvmet_tcp_try_send_one() sends queue->snd_cmd; cmd->state says which PDU
 * goes out, and cmd->offset == 0 marks its first byte (a PDU may need several
 * sendmsg calls).
 */
SEC("kprobe/sock_sendmsg")
int BPF_KPROBE(ntp_t_sendmsg, struct socket* sock) {
  u64 tid = ntp_tid();
  struct io_ctx* c = bpf_map_lookup_elem(&io_ctx, &tid);
  struct nvmet_tcp_queue* queue;
  struct nvmet_tcp_cmd* cmd;
  struct ntp_tgt_rec* rec;
  struct nvmet_req* req;
  u64 now;

  if (!c) return 0;
  queue = (void*)c->queue;
  if ((u64)BPF_CORE_READ(queue, sock) != (u64)sock) return 0;
  cmd = BPF_CORE_READ(queue, snd_cmd);
  if (!cmd || BPF_CORE_READ(cmd, offset)) return 0;

  req = req_of(cmd);
  rec = rec_of(req);
  now = bpf_ktime_get_ns();

  switch (BPF_CORE_READ(cmd, state)) {
    case NVMET_TCP_SEND_R2T:
      if (rec) ntp_stamp_once(&rec->ts[NTP_T_R2T_SENT], now);
      break;
    case NVMET_TCP_SEND_DATA_PDU:
      if (rec) ntp_stamp_once(&rec->ts[NTP_T_C2H_SENT], now);
      /* With SQ head pointer updates disabled no response capsule follows. */
      if (BPF_CORE_READ(queue, nvme_sq.sqhd_disabled)) {
        if (rec) rec->hdr.flags |= NTP_F_DATA_SUCCESS;
        finish(req, rec);
      }
      break;
    case NVMET_TCP_SEND_RESPONSE:
      if (rec) rec->ts[NTP_T_RSP_SENT] = now;
      finish(req, rec);
      break;
    default:
      break;
  }
  return 0;
}
