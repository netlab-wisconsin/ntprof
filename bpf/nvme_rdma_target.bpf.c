// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/RDMA target side (drivers/nvme/target/rdma.c, unmodified).
 *
 * The record and the nvmet-core / backend / block-layer hooks are in
 * target.bpf.h; the NVMe/RDMA hooks here:
 *
 *   nvmet_rdma_recv_done entry / exit  -> context and NTP_T_CMD_ARR (the CQE)
 *   nvmet_rdma_execute_command         -> NTP_T_R2T_SENT (RDMA READ posted)
 *   nvmet_rdma_read_data_done          -> NTP_T_DATA_* (the write data arrived)
 *   nvmet_rdma_queue_response entry    -> NTP_T_RSP_QUEUED
 *   nvmet_rdma_queue_response exit     -> NTP_T_C2H_SENT / NTP_T_RSP_SENT
 *                                         (data and response posted)
 *   nvmet_rdma_send_done               -> NTP_T_SEND_DONE (the response's
 *                                         send completion; for a read, after
 *                                         its data's RDMA WRITE)
 *
 * The record is emitted at the later of the posting's return and the send
 * completion: the completion can be handled on another CPU first.
 */
#define NTP_TRANSPORT NTP_TR_RDMA
#include "target.bpf.h"

/* Commands arrive under the receive completion handler's context. */
static __always_inline bool tgt_is_ours(struct nvmet_req* req, struct nvmet_sq* sq) {
  return NTP_CTX_GET(tgt_ctx) != NULL;
}

static __always_inline u64 tgt_sampled(struct nvmet_req* req, struct nvmet_sq* sq,
                                       u64 now) {
  struct tgt_ctx* c = NTP_CTX_GET(tgt_ctx);

  return c ? tgt_arrival(c, now) : now;
}

/* queue_response entry -> return (kretprobe flavour only) */
struct resp_ctx {
  u64 tid;
  u64 req;
};
NTP_CPU_CTX(resp_ctx, struct resp_ctx);

/* nvmet_req of an nvmet_rdma_rsp, as a plain number */
static __always_inline u64 req_of_rsp(const void* rsp) {
  return ntp_scalar((u64)rsp + bpf_core_field_offset(struct nvmet_rdma_rsp, req));
}

/* ---- command arrival ---- */

static __always_inline int on_recv_done(const struct ib_wc* wc) {
  tgt_ctx_enter((u64)BPF_CORE_READ(wc, qp, qp_context), 0, bpf_ktime_get_ns());
  return 0;
}

SEC("fentry/nvmet_rdma_recv_done")
int BPF_PROG(ntp_tr_recv_done_fe, void* cq, struct ib_wc* wc) {
  return on_recv_done(wc);
}

SEC("fexit/nvmet_rdma_recv_done")
int BPF_PROG(ntp_tr_recv_done_fx) { return tgt_ctx_exit(); }

SEC("kprobe/nvmet_rdma_recv_done")
int BPF_KPROBE(ntp_tr_recv_done_kp, void* cq, struct ib_wc* wc) {
  return on_recv_done(wc);
}

SEC("kretprobe/nvmet_rdma_recv_done")
int BPF_KRETPROBE(ntp_tr_recv_done_kr) { return tgt_ctx_exit(); }

/* ---- write data: fetched with RDMA READ ---- */

/*
 * nvmet_rdma_need_data_in(): a write whose data is not in the capsule. Its
 * RDMA READ is posted inside this function (rdma_rw_ctx_post).
 */
static __always_inline int on_execute(const struct nvmet_rdma_rsp* rsp) {
  struct ntp_tgt_rec* rec;
  struct nvmet_req* req;
  u64 key;

  if (!ntp_inflight) return 0;
  key = req_of_rsp(rsp);
  rec = tgt_rec(key);
  if (!rec || !(rec->hdr.flags & NTP_F_WRITE)) return 0;
  req = (void*)key;
  if (!BPF_CORE_READ(req, transfer_len) ||
      (BPF_CORE_READ(rsp, flags) & NVMET_RDMA_REQ_INLINE_DATA)) {
    rec->hdr.flags |= NTP_F_INLINE_DATA;
    return 0;
  }
  rec->hdr.flags |= NTP_F_RDMA_READ;
  ntp_stamp_once(&rec->ts[NTP_T_R2T_SENT], bpf_ktime_get_ns());
  return 0;
}

SEC("fentry/nvmet_rdma_execute_command")
int BPF_PROG(ntp_tr_execute_fe, struct nvmet_rdma_rsp* rsp) {
  return on_execute(rsp);
}

SEC("kprobe/nvmet_rdma_execute_command")
int BPF_KPROBE(ntp_tr_execute_kp, struct nvmet_rdma_rsp* rsp) {
  return on_execute(rsp);
}

static __always_inline int on_read_done(const struct ib_wc* wc) {
  struct ntp_tgt_rec* rec;
  void* rsp;
  u64 now;

  if (!ntp_inflight) return 0;
  rsp = (void*)BPF_CORE_READ(wc, wr_cqe) -
        bpf_core_field_offset(struct nvmet_rdma_rsp, read_cqe);
  rec = tgt_rec(req_of_rsp(rsp));
  if (!rec || rec->ts[NTP_T_DATA_PROC]) return 0;
  now = bpf_ktime_get_ns();
  rec->ts[NTP_T_DATA_ARR] = now;
  rec->ts[NTP_T_DATA_PROC] = now;
  return 0;
}

SEC("fentry/nvmet_rdma_read_data_done")
int BPF_PROG(ntp_tr_read_done_fe, void* cq, struct ib_wc* wc) {
  return on_read_done(wc);
}

SEC("kprobe/nvmet_rdma_read_data_done")
int BPF_KPROBE(ntp_tr_read_done_kp, void* cq, struct ib_wc* wc) {
  return on_read_done(wc);
}

/* ---- response ---- */

static __always_inline int on_queue_resp(const struct nvmet_req* req) {
  struct ntp_tgt_rec* rec;
  u64 key;

  if (ntp_idle()) return 0;
  key = ntp_scalar((u64)req);
  if (filter.track_queues) count_resp_queued(key);
  rec = tgt_rec(key);
  if (rec) ntp_stamp_once(&rec->ts[NTP_T_RSP_QUEUED], bpf_ktime_get_ns());
  return 0;
}

#define RSP_POSTED 1u    /* nvmet_rdma_queue_response() returned */
#define RSP_SEND_DONE 2u /* the send completion was handled */

/* One end of the response was seen: emit once both were (atomic: two CPUs). */
static __always_inline void rsp_part_done(u64 req, u32 part) {
  struct tgt_entry* e = tgt_entry(req);

  if (e && (__sync_fetch_and_or(&e->parts, part) | part) == (RSP_POSTED | RSP_SEND_DONE))
    tgt_emit(req);
}

/* Data (RDMA WRITE, for a read) and response (SEND) are posted together. */
static __always_inline int on_queue_resp_exit(u64 req) {
  struct ntp_tgt_rec* rec;
  u64 now;

  if (ntp_idle()) return 0;
  rec = tgt_rec(req);
  if (rec) {
    now = bpf_ktime_get_ns();
    if (!(rec->hdr.flags & NTP_F_WRITE)) rec->ts[NTP_T_C2H_SENT] = now;
    rec->ts[NTP_T_RSP_SENT] = now;
    rsp_part_done(req, RSP_POSTED);
  }
  tgt_uncount(req);
  return 0;
}

/* The response's send completion (signaled on every response). */
static __always_inline int on_send_done(const struct ib_wc* wc) {
  struct ntp_tgt_rec* rec;
  void* rsp;
  u64 req;

  if (!ntp_inflight) return 0;
  rsp = (void*)BPF_CORE_READ(wc, wr_cqe) -
        bpf_core_field_offset(struct nvmet_rdma_rsp, send_cqe);
  req = req_of_rsp(rsp);
  rec = tgt_rec(req);
  if (!rec) return 0;
  ntp_stamp_once(&rec->ts[NTP_T_SEND_DONE], bpf_ktime_get_ns());
  rsp_part_done(req, RSP_SEND_DONE);
  return 0;
}

SEC("fentry/nvmet_rdma_send_done")
int BPF_PROG(ntp_tr_send_done_fe, void* cq, struct ib_wc* wc) {
  return on_send_done(wc);
}

SEC("kprobe/nvmet_rdma_send_done")
int BPF_KPROBE(ntp_tr_send_done_kp, void* cq, struct ib_wc* wc) {
  return on_send_done(wc);
}

SEC("fentry/nvmet_rdma_queue_response")
int BPF_PROG(ntp_tr_queue_resp_fe, struct nvmet_req* req) {
  return on_queue_resp(req);
}

SEC("fexit/nvmet_rdma_queue_response")
int BPF_PROG(ntp_tr_queue_resp_fx, struct nvmet_req* req) {
  return on_queue_resp_exit(ntp_scalar((u64)req));
}

SEC("kprobe/nvmet_rdma_queue_response")
int BPF_KPROBE(ntp_tr_queue_resp_kp, struct nvmet_req* req) {
  u32 zero = 0;
  struct resp_ctx* c = bpf_map_lookup_elem(&resp_ctx, &zero);

  if (c) {
    c->tid = ntp_tid();
    c->req = (u64)req;
  }
  return on_queue_resp(req);
}

SEC("kretprobe/nvmet_rdma_queue_response")
int BPF_KRETPROBE(ntp_tr_queue_resp_kr) {
  struct resp_ctx* c = NTP_CTX_GET(resp_ctx);

  if (!c) return 0;
  c->tid = 0;
  return on_queue_resp_exit(c->req);
}
