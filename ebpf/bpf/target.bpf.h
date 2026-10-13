/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Target side, common to every NVMe-oF transport: the command record from
 * its arrival to its response, keyed by the command's struct nvmet_req, and
 * the hooks in nvmet-core, the backends and the block layer.
 *
 * A transport object defines NTP_TRANSPORT and includes this file once, then
 * sets the target context (tgt_ctx_enter/exit) around the code that receives
 * commands, so that the nvmet_req_init tracepoint knows the command is its
 * own and when it arrived, and stamps the transport's own events.
 */
#ifndef NTPROF_TARGET_BPF_H
#define NTPROF_TARGET_BPF_H

#include "common.bpf.h"

#ifndef NTP_TRANSPORT
#error "define NTP_TRANSPORT before including target.bpf.h"
#endif

struct tgt_entry {
  u64 req; /* struct nvmet_req * owning the slot; 0 = free */
  struct ntp_tgt_rec rec;
};

/*
 * Records by struct nvmet_req (common.bpf.h: direct-mapped tables). A
 * command's nvmet_req is reused only after its response has been sent, when
 * the record has been emitted. The bdev backend sets bio->bi_private to it.
 */
struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct tgt_entry);
} treqs SEC(".maps");

/*
 * Exact per-queue occupancy over all commands (filter.track_queues, off by
 * default). A command is counted when nvmet_req_init() sees it after arming,
 * so every decrement matches an increment.
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
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 65536);
  __type(key, u64); /* struct nvmet_req * */
  __type(value, struct tcounted);
} tcounted SEC(".maps");

/*
 * Target context: set while the transport receives commands on this thread.
 * `queue` identifies the transport queue, `sk` its socket (TCP), and
 * `last_skb` / `arrival` say when the data being processed arrived.
 */
struct tgt_ctx {
  u64 tid;
  u64 queue;
  u64 sk;
  u64 last_skb; /* TCP: the last skb copied; its arrival is looked up lazily */
  u64 arrival;  /* RDMA: when the receive completion was processed */
};
NTP_CPU_CTX(tgt_ctx, struct tgt_ctx);

static __always_inline void tgt_ctx_enter(u64 queue, u64 sk, u64 arrival) {
  u32 zero = 0;
  struct tgt_ctx* c = bpf_map_lookup_elem(&tgt_ctx, &zero);

  if (!c) return;
  c->tid = ntp_tid();
  c->queue = queue;
  c->sk = sk;
  c->last_skb = 0;
  c->arrival = arrival;
}

static __always_inline int tgt_ctx_exit(void) {
  struct tgt_ctx* c = NTP_CTX_GET(tgt_ctx);

  if (c) c->tid = 0;
  return 0;
}

/* When the data now being processed reached this machine. */
static __always_inline u64 tgt_arrival(const struct tgt_ctx* c, u64 now) {
  if (c->last_skb) return ntp_skb_arrival((void*)c->last_skb, now);
  return c->arrival ? c->arrival : now;
}

/* The record of @req (a plain number, see ntp_scalar), or NULL. */
static __always_inline struct tgt_entry* tgt_entry(u64 req) {
  u32 slot = ntp_slot(req, NTP_REC_SLOTS);
  struct tgt_entry* e = bpf_map_lookup_elem(&treqs, &slot);

  return (e && e->req == req) ? e : NULL;
}

static __always_inline struct ntp_tgt_rec* tgt_rec(u64 req) {
  struct tgt_entry* e = tgt_entry(req);

  return e ? &e->rec : NULL;
}

static __always_inline struct tqstat* get_tqstat(u64 sq) {
  struct tqstat* qs = bpf_map_lookup_elem(&tqstats, &sq);

  if (!qs) {
    struct tqstat zero = {};

    bpf_map_update_elem(&tqstats, &sq, &zero, BPF_NOEXIST);
    qs = bpf_map_lookup_elem(&tqstats, &sq);
  }
  return qs;
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

/* A completion was queued for the transport to send: count it. */
static __always_inline void count_resp_queued(u64 req) {
  struct tcounted* tc = bpf_map_lookup_elem(&tcounted, &req);

  if (tc && !(tc->flags & CNT_RESP_QUEUED)) {
    struct tqstat* qs = bpf_map_lookup_elem(&tqstats, &tc->sq);

    tc->flags |= CNT_RESP_QUEUED;
    if (qs) __sync_fetch_and_add(&qs->resp_queue, 1);
  }
}

/* ---- command arrival ---- */

SEC("raw_tp/nvmet_req_init")
int BPF_PROG(ntp_t_req_init, struct nvmet_req* req, void* cmd) {
  struct tgt_ctx* c = NTP_CTX_GET(tgt_ctx);
  struct ntp_cmd_id id = {};
  struct ntp_tgt_rec* rec;
  struct tgt_entry* e;
  struct nvmet_sq* sq;
  u16 q_outstanding = 0, q_resp_queue = 0;
  u64 now, key = (u64)req;
  u32 slot;

  if (!c || !ntp_armed) return 0; /* not this transport, or not armed yet */
  sq = BPF_CORE_READ(req, sq);
  if (!BPF_CORE_READ(sq, qid)) return 0; /* admin queue */

  if (filter.track_queues) {
    struct tcounted tc = {.sq = (u64)sq, .flags = CNT_OUTSTANDING};
    struct tqstat* qs = get_tqstat((u64)sq);

    /* the slot's previous command, if its response was never seen */
    uncount(key, CNT_OUTSTANDING | CNT_RESP_QUEUED, true);
    if (qs) {
      q_outstanding = ntp_clamp_u16(qs->outstanding);
      q_resp_queue = ntp_clamp_u16(qs->resp_queue);
      bpf_map_update_elem(&tcounted, &key, &tc, BPF_ANY);
      __sync_fetch_and_add(&qs->outstanding, 1);
    }
  }
  ntp_count(NTP_CNT_TGT_CMDS, 1);

  if (ntp_read_cmd(cmd, &id) || !ntp_is_rw(id.opcode)) return 0;
  if (filter.io_type != 2 && filter.io_type != (id.opcode == NTP_NVME_CMD_WRITE))
    return 0;
  if (!ntp_sampled(id.cid, id.slba, id.nlb)) return 0;

  slot = ntp_slot(key, NTP_REC_SLOTS);
  e = bpf_map_lookup_elem(&treqs, &slot);
  if (!e) return 0;
  if (e->req)
    ntp_count(NTP_CNT_STALE, 1);
  else
    ntp_inflight_add(1);
  e->req = 0;
  __builtin_memset(&e->rec, 0, sizeof(e->rec));
  rec = &e->rec;
  now = bpf_ktime_get_ns();
  rec->id = id;
  rec->hdr.kind = NTP_REC_TARGET;
  rec->hdr.transport = NTP_TRANSPORT;
  if (id.opcode == NTP_NVME_CMD_WRITE) rec->hdr.flags |= NTP_F_WRITE;
  if (filter.track_queues) rec->hdr.flags |= NTP_F_QUEUES;
  rec->id.cntlid = BPF_CORE_READ(sq, ctrl, cntlid);
  rec->id.qid = BPF_CORE_READ(sq, qid);
  rec->q_outstanding = q_outstanding;
  rec->q_resp_queue = q_resp_queue;
  rec->ts[NTP_T_CMD_ARR] = tgt_arrival(c, now);
  rec->ts[NTP_T_CMD_PROC] = now;
  e->req = key; /* last: the slot is ours from here on */
  ntp_count(NTP_CNT_TGT_SAMPLED, 1);
  return 0;
}

/* ---- backend ---- */

static __always_inline int on_exec(const struct nvmet_req* req) {
  struct ntp_tgt_rec* rec;

  if (!ntp_inflight) return 0;
  rec = tgt_rec(ntp_scalar((u64)req));
  if (!rec || rec->ts[NTP_T_EXEC]) return 0;
  rec->ts[NTP_T_EXEC] = bpf_ktime_get_ns();
  /* parsed after the nvmet_req_init tracepoint fired */
  rec->bytes = BPF_CORE_READ(req, transfer_len);
  return 0;
}

SEC("fentry/nvmet_bdev_execute_rw")
int BPF_PROG(ntp_t_exec_bdev_fe, struct nvmet_req* req) { return on_exec(req); }

SEC("kprobe/nvmet_bdev_execute_rw")
int BPF_KPROBE(ntp_t_exec_bdev_kp, struct nvmet_req* req) { return on_exec(req); }

SEC("fentry/nvmet_file_execute_rw")
int BPF_PROG(ntp_t_exec_file_fe, struct nvmet_req* req) { return on_exec(req); }

SEC("kprobe/nvmet_file_execute_rw")
int BPF_KPROBE(ntp_t_exec_file_kp, struct nvmet_req* req) { return on_exec(req); }

SEC("fentry/nvmet_passthru_execute_cmd")
int BPF_PROG(ntp_t_exec_pt_fe, struct nvmet_req* req) { return on_exec(req); }

SEC("kprobe/nvmet_passthru_execute_cmd")
int BPF_KPROBE(ntp_t_exec_pt_kp, struct nvmet_req* req) { return on_exec(req); }

/* The bdev backend sets bio->bi_private to the nvmet_req. */
static __always_inline struct ntp_tgt_rec* rec_of_rq(const struct request* rq) {
  u64 priv;

  if (!ntp_inflight) return NULL;
  priv = (u64)BPF_CORE_READ(rq, bio, bi_private);
  return priv ? tgt_rec(priv) : NULL;
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
  struct ntp_tgt_rec* rec;

  if (!ntp_inflight) return 0;
  rec = tgt_rec((u64)req);
  if (rec) ntp_stamp_once(&rec->ts[NTP_T_COMPLETE], bpf_ktime_get_ns());
  return 0;
}

/* ---- response ---- */

/* The command's last message is going out: emit its record and free it. */
static __always_inline void tgt_finish(u64 req) {
  struct tgt_entry* e;

  if (filter.track_queues) uncount(req, CNT_OUTSTANDING | CNT_RESP_QUEUED, true);
  e = tgt_entry(req);
  if (!e) return;
  e->rec.hdr.cpu = bpf_get_smp_processor_id();
  if (bpf_ringbuf_output(&events, &e->rec, sizeof(e->rec), 0))
    ntp_count(NTP_CNT_RB_DROPS, 1);
  else
    ntp_count(NTP_CNT_TGT_EMITTED, 1);
  e->req = 0;
  ntp_inflight_add(-1);
}

#endif /* NTPROF_TARGET_BPF_H */
