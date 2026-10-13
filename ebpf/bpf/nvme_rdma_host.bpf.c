// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/RDMA initiator side (drivers/nvme/host/rdma.c, unmodified).
 *
 * The record and the block-layer / nvme-core hooks are in host.bpf.h; the
 * NVMe/RDMA hooks here:
 *
 *   nvme_rdma_queue_rq                 -> NTP_H_QUEUE_RQ, queue, qid
 *   nvme_rdma_post_send returning 0    -> NTP_H_CMD_SENT (command posted)
 *   nvme_rdma_send_done                -> NTP_H_SEND_DONE (its send completion)
 *   nvme_rdma_recv_done                -> NTP_H_RSP_ARR (the response's CQE)
 *   blk_mq_complete_request_remote     -> NTP_H_RSP_PROC
 *
 * A request completes when both its send completion and its response have
 * been seen (nvme_rdma_end_request), or when a local invalidation finishes;
 * each of these handlers sets the receive context.
 *
 * Over RDMA the host CPU sees neither the data of a read (the target writes it
 * into host memory) nor that of a write (the target reads it): the host's
 * wait covers network, target and data transfer.
 */
#define NTP_TRANSPORT NTP_TR_RDMA
#include "host.bpf.h"

/* post_send entry -> return (kretprobe flavour only) */
struct post_ctx {
  u64 tid;
  u64 qe;
};
NTP_CPU_CTX(post_ctx, struct post_ctx);

/* struct request of a request's send qe (the async event qe is not one) */
static __always_inline u64 rq_of_sqe(const struct nvme_rdma_qe* qe) {
  return rq_from_pdu((void*)qe - bpf_core_field_offset(struct nvme_rdma_request, sqe));
}

/* ---- submission ---- */

SEC("fentry/nvme_rdma_queue_rq")
int BPF_PROG(ntp_hr_queue_rq_fe, struct blk_mq_hw_ctx* hctx,
             const struct blk_mq_queue_data* bd) {
  return host_queue_rq(hctx, bd);
}

SEC("kprobe/nvme_rdma_queue_rq")
int BPF_KPROBE(ntp_hr_queue_rq_kp, struct blk_mq_hw_ctx* hctx,
               const struct blk_mq_queue_data* bd) {
  return host_queue_rq(hctx, bd);
}

/* The command (after a memory registration chained before it) is posted. */
static __always_inline int on_posted(const struct nvme_rdma_qe* qe, int ret) {
  struct ntp_host_rec* rec;
  u64 rq;

  if (ret || ntp_idle()) return 0;
  rq = rq_of_sqe(qe);
  if (filter.track_queues) uncount(rq, CNT_UNSENT, false);
  rec = host_rec(rq);
  if (rec) ntp_stamp_once(&rec->ts[NTP_H_CMD_SENT], bpf_ktime_get_ns());
  return 0;
}

SEC("fexit/nvme_rdma_post_send")
int BPF_PROG(ntp_hr_post_fx, void* queue, struct nvme_rdma_qe* qe, void* sge,
             u32 num_sge, void* first, int ret) {
  return on_posted(qe, ret);
}

SEC("kprobe/nvme_rdma_post_send")
int BPF_KPROBE(ntp_hr_post_kp, void* queue, struct nvme_rdma_qe* qe) {
  u32 zero = 0;
  struct post_ctx* c = bpf_map_lookup_elem(&post_ctx, &zero);

  if (c) {
    c->tid = ntp_tid();
    c->qe = (u64)qe;
  }
  return 0;
}

SEC("kretprobe/nvme_rdma_post_send")
int BPF_KRETPROBE(ntp_hr_post_kr, int ret) {
  struct post_ctx* c = NTP_CTX_GET(post_ctx);

  if (!c) return 0;
  c->tid = 0;
  return on_posted((void*)c->qe, ret);
}

/* ---- completions ---- */

/* Send completion of the command capsule; may complete the request. */
static __always_inline int on_send_done(const struct ib_wc* wc) {
  struct ib_cqe* cqe;
  struct ntp_host_rec* rec;

  if (ntp_idle()) return 0;
  cqe = BPF_CORE_READ(wc, wr_cqe);
  rec = host_rec(rq_of_sqe((void*)cqe));
  if (rec) ntp_stamp_once(&rec->ts[NTP_H_SEND_DONE], bpf_ktime_get_ns());
  host_rx_enter((u64)BPF_CORE_READ(wc, qp, qp_context), 0, 0);
  return 0;
}

/* The response capsule arrived; the CQE in the receive buffer names it. */
static __always_inline int on_recv_done(const struct ib_wc* wc) {
  struct nvme_rdma_qe* qe;
  struct ntp_host_rec* rec;
  u64 queue;
  u16 cid = 0;
  void* data;

  if (ntp_idle()) return 0;
  queue = (u64)BPF_CORE_READ(wc, qp, qp_context);
  qe = (void*)BPF_CORE_READ(wc, wr_cqe); /* cqe is the first member */
  data = BPF_CORE_READ(qe, data);
  if (!bpf_probe_read_kernel(&cid, sizeof(cid),
                             data + __builtin_offsetof(struct ntp_nvme_cqe,
                                                       command_id))) {
    rec = host_rec_by_cid(queue, cid);
    if (rec) ntp_stamp_once(&rec->ts[NTP_H_RSP_ARR], bpf_ktime_get_ns());
  }
  host_rx_enter(queue, 0, 0);
  return 0;
}

/* A local invalidation finished; it completes the request. */
static __always_inline int on_inv_done(const struct ib_wc* wc) {
  if (!ntp_idle()) host_rx_enter((u64)BPF_CORE_READ(wc, qp, qp_context), 0, 0);
  return 0;
}

SEC("fentry/nvme_rdma_send_done")
int BPF_PROG(ntp_hr_send_done_fe, void* cq, struct ib_wc* wc) {
  return on_send_done(wc);
}

SEC("fexit/nvme_rdma_send_done")
int BPF_PROG(ntp_hr_send_done_fx) { return host_rx_exit(); }

SEC("kprobe/nvme_rdma_send_done")
int BPF_KPROBE(ntp_hr_send_done_kp, void* cq, struct ib_wc* wc) {
  return on_send_done(wc);
}

SEC("kretprobe/nvme_rdma_send_done")
int BPF_KRETPROBE(ntp_hr_send_done_kr) { return host_rx_exit(); }

SEC("fentry/nvme_rdma_recv_done")
int BPF_PROG(ntp_hr_recv_done_fe, void* cq, struct ib_wc* wc) {
  return on_recv_done(wc);
}

SEC("fexit/nvme_rdma_recv_done")
int BPF_PROG(ntp_hr_recv_done_fx) { return host_rx_exit(); }

SEC("kprobe/nvme_rdma_recv_done")
int BPF_KPROBE(ntp_hr_recv_done_kp, void* cq, struct ib_wc* wc) {
  return on_recv_done(wc);
}

SEC("kretprobe/nvme_rdma_recv_done")
int BPF_KRETPROBE(ntp_hr_recv_done_kr) { return host_rx_exit(); }

SEC("fentry/nvme_rdma_inv_rkey_done")
int BPF_PROG(ntp_hr_inv_done_fe, void* cq, struct ib_wc* wc) {
  return on_inv_done(wc);
}

SEC("fexit/nvme_rdma_inv_rkey_done")
int BPF_PROG(ntp_hr_inv_done_fx) { return host_rx_exit(); }

SEC("kprobe/nvme_rdma_inv_rkey_done")
int BPF_KPROBE(ntp_hr_inv_done_kp, void* cq, struct ib_wc* wc) {
  return on_inv_done(wc);
}

SEC("kretprobe/nvme_rdma_inv_rkey_done")
int BPF_KRETPROBE(ntp_hr_inv_done_kr) { return host_rx_exit(); }

SEC("fentry/blk_mq_complete_request_remote")
int BPF_PROG(ntp_hr_complete_fe, struct request* rq) {
  host_complete(rq);
  return 0;
}

SEC("kprobe/blk_mq_complete_request_remote")
int BPF_KPROBE(ntp_hr_complete_kp, struct request* rq) {
  host_complete(rq);
  return 0;
}
