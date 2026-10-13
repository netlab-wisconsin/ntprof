// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP initiator side (drivers/nvme/host/tcp.c, unmodified).
 *
 * The record and the block-layer / nvme-core hooks are in host.bpf.h; the
 * NVMe/TCP hooks here (see ebpf/DESIGN.md for the mapping to v1's
 * tracepoints):
 *
 *   nvme_tcp_queue_rq                  -> NTP_H_QUEUE_RQ, queue, qid
 *   nvme_tcp_try_send_{cmd_pdu,data,ddgst} returning 1
 *                                      -> NTP_H_CMD_SENT / NTP_H_DATA_SENT
 *   nvme_tcp_try_send_data_pdu         -> NTP_H_DATA_START
 *   nvme_tcp_recv_skb entry / exit     -> receive context (queue, skb arrival)
 *   skb_copy_datagram_iter             -> NTP_H_C2H_* (first copy of a C2H PDU)
 *   nvme_tcp_setup_h2c_data_pdu        -> NTP_H_R2T_* (when called while receiving)
 *   blk_mq_complete_request_remote     -> NTP_H_RSP_* (CQE parsed)
 *
 * Function hooks exist as fentry/fexit and as kprobe/kretprobe programs with
 * the same body (common.bpf.h).
 */
#define NTP_TRANSPORT NTP_TR_TCP
#include "host.bpf.h"

/* try_send_* entry -> return (kretprobe flavour only) */
struct send_ctx {
  u64 tid;
  u64 req;
};
NTP_CPU_CTX(send_ctx, struct send_ctx);

/* ---- submission ---- */

SEC("fentry/nvme_tcp_queue_rq")
int BPF_PROG(ntp_h_queue_rq_fe, struct blk_mq_hw_ctx* hctx,
             const struct blk_mq_queue_data* bd) {
  return host_queue_rq(hctx, bd);
}

SEC("kprobe/nvme_tcp_queue_rq")
int BPF_KPROBE(ntp_h_queue_rq_kp, struct blk_mq_hw_ctx* hctx,
               const struct blk_mq_queue_data* bd) {
  return host_queue_rq(hctx, bd);
}

/*
 * A try_send function returned. 1 means the PDU (or data) it was sending went
 * out completely; nvme_tcp_done_send_req() then cleared queue->request if the
 * whole request was sent. send_mutex is still held, so queue->request cannot
 * have been refilled yet.
 */
static __always_inline int on_send_done(struct nvme_tcp_request* req, int ret,
                                        bool cmd_pdu) {
  struct nvme_tcp_queue* queue;
  struct ntp_host_rec* rec;
  u64 rq, now;

  if (ret != 1 || ntp_idle()) return 0;
  rq = rq_from_pdu(req);
  if (cmd_pdu && filter.track_queues) uncount(rq, CNT_UNSENT, false);

  queue = BPF_CORE_READ(req, queue);
  if (BPF_CORE_READ(queue, request)) return 0; /* more of this request to send */

  rec = host_rec(rq);
  if (!rec) return 0;
  now = bpf_ktime_get_ns();
  if (!rec->ts[NTP_H_CMD_SENT])
    rec->ts[NTP_H_CMD_SENT] = now;
  else if (rec->ts[NTP_H_R2T_PROC])
    rec->ts[NTP_H_DATA_SENT] = now; /* the last solicited data PDU */
  return 0;
}

/* kretprobe flavour: remember the request at entry */
static __always_inline int send_enter(const struct nvme_tcp_request* req) {
  u32 zero = 0;
  struct send_ctx* c = bpf_map_lookup_elem(&send_ctx, &zero);

  if (c) {
    c->tid = ntp_tid();
    c->req = (u64)req;
  }
  return 0;
}

static __always_inline int send_exit(int ret, bool cmd_pdu) {
  struct send_ctx* c = NTP_CTX_GET(send_ctx);

  if (!c) return 0;
  c->tid = 0;
  return on_send_done((void*)c->req, ret, cmd_pdu);
}

/* The try_send functions return int: only the low 32 bits of ax are defined. */
SEC("fexit/nvme_tcp_try_send_cmd_pdu")
int BPF_PROG(ntp_h_send_cmd_fx, struct nvme_tcp_request* req, int ret) {
  return on_send_done(req, ret, true);
}

SEC("kprobe/nvme_tcp_try_send_cmd_pdu")
int BPF_KPROBE(ntp_h_send_cmd_kp, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_cmd_pdu")
int BPF_KRETPROBE(ntp_h_send_cmd_kr, int ret) { return send_exit(ret, true); }

SEC("fexit/nvme_tcp_try_send_data")
int BPF_PROG(ntp_h_send_data_fx, struct nvme_tcp_request* req, int ret) {
  return on_send_done(req, ret, false);
}

SEC("kprobe/nvme_tcp_try_send_data")
int BPF_KPROBE(ntp_h_send_data_kp, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_data")
int BPF_KRETPROBE(ntp_h_send_data_kr, int ret) { return send_exit(ret, false); }

SEC("fexit/nvme_tcp_try_send_ddgst")
int BPF_PROG(ntp_h_send_ddgst_fx, struct nvme_tcp_request* req, int ret) {
  return on_send_done(req, ret, false);
}

SEC("kprobe/nvme_tcp_try_send_ddgst")
int BPF_KPROBE(ntp_h_send_ddgst_kp, struct nvme_tcp_request* req) {
  return send_enter(req);
}

SEC("kretprobe/nvme_tcp_try_send_ddgst")
int BPF_KRETPROBE(ntp_h_send_ddgst_kr, int ret) {
  return send_exit(ret, false);
}

/* The H2C data PDU header of an R2T: the host starts sending the data. */
static __always_inline int on_send_h2c(struct nvme_tcp_request* req) {
  struct ntp_host_rec* rec;

  if (!ntp_inflight) return 0;
  rec = host_rec(rq_from_pdu(req));
  if (rec && rec->ts[NTP_H_R2T_PROC])
    ntp_stamp_once(&rec->ts[NTP_H_DATA_START], bpf_ktime_get_ns());
  return 0;
}

SEC("fentry/nvme_tcp_try_send_data_pdu")
int BPF_PROG(ntp_h_send_h2c_fe, struct nvme_tcp_request* req) {
  return on_send_h2c(req);
}

SEC("kprobe/nvme_tcp_try_send_data_pdu")
int BPF_KPROBE(ntp_h_send_h2c_kp, struct nvme_tcp_request* req) {
  return on_send_h2c(req);
}

/* ---- receive path ---- */

static __always_inline int on_recv_skb(struct sk_buff* skb) {
  /* without a record in flight nothing received can need a timestamp */
  if (ntp_idle()) return 0;
  /* nvme_tcp_setup_sock_ops() stores the queue in sk_user_data */
  host_rx_enter((u64)BPF_CORE_READ(skb, sk, sk_user_data), (u64)skb,
                ntp_skb_arrival(skb, bpf_ktime_get_ns()));
  return 0;
}

SEC("fentry/nvme_tcp_recv_skb")
int BPF_PROG(ntp_h_recv_skb_fe, void* desc, struct sk_buff* skb) {
  return on_recv_skb(skb);
}

SEC("fexit/nvme_tcp_recv_skb")
int BPF_PROG(ntp_h_recv_skb_fx) { return host_rx_exit(); }

SEC("kprobe/nvme_tcp_recv_skb")
int BPF_KPROBE(ntp_h_recv_skb_kp, void* desc, struct sk_buff* skb) {
  return on_recv_skb(skb);
}

SEC("kretprobe/nvme_tcp_recv_skb")
int BPF_KRETPROBE(ntp_h_recv_skb_kr) { return host_rx_exit(); }

/* First data copy of a C2H data PDU: the PDU header in queue->pdu names it. */
static __always_inline int on_copy(const struct sk_buff* skb) {
  struct host_rx* c = NTP_CTX_GET(rx_ctx);
  struct ntp_tcp_data_pdu pdu;
  struct nvme_tcp_queue* queue;
  struct ntp_host_rec* rec;
  void* hdr;

  if (!c || c->skb != (u64)skb) return 0;
  queue = (void*)c->queue;
  hdr = BPF_CORE_READ(queue, pdu);
  if (bpf_probe_read_kernel(&pdu, sizeof(pdu), hdr)) return 0;
  if (pdu.hdr.type != nvme_tcp_c2h_data) return 0;

  rec = host_rec_by_cid(c->queue, pdu.command_id);
  if (!rec || rec->ts[NTP_H_C2H_PROC]) return 0;
  rec->ts[NTP_H_C2H_ARR] = c->arrival;
  rec->ts[NTP_H_C2H_PROC] = bpf_ktime_get_ns();
  return 0;
}

SEC("fentry/skb_copy_datagram_iter")
int BPF_PROG(ntp_h_copy_fe, const struct sk_buff* skb) { return on_copy(skb); }

SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_h_copy_kp, const struct sk_buff* skb) { return on_copy(skb); }

/* the variant used with data digests */
SEC("fentry/skb_copy_and_hash_datagram_iter")
int BPF_PROG(ntp_h_copy_hash_fe, const struct sk_buff* skb) {
  return on_copy(skb);
}

SEC("kprobe/skb_copy_and_hash_datagram_iter")
int BPF_KPROBE(ntp_h_copy_hash_kp, const struct sk_buff* skb) {
  return on_copy(skb);
}

/* Called by nvme_tcp_handle_r2t() (receive path) and by the send path. */
static __always_inline int on_r2t(struct nvme_tcp_request* req) {
  struct host_rx* c = NTP_CTX_GET(rx_ctx);
  struct ntp_host_rec* rec;

  if (!c) return 0;
  rec = host_rec(rq_from_pdu(req));
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

SEC("fentry/nvme_tcp_setup_h2c_data_pdu")
int BPF_PROG(ntp_h_r2t_fe, struct nvme_tcp_request* req) { return on_r2t(req); }

SEC("kprobe/nvme_tcp_setup_h2c_data_pdu")
int BPF_KPROBE(ntp_h_r2t_kp, struct nvme_tcp_request* req) {
  return on_r2t(req);
}

/* A C2H PDU with the SUCCESS flag completes a read without a response. */
static __always_inline int on_complete(struct request* rq) {
  struct ntp_host_rec* rec = host_complete(rq);
  struct host_rx* c;

  if (!rec || !rec->ts[NTP_H_C2H_PROC]) return 0;
  c = NTP_CTX_GET(rx_ctx);
  if (c) {
    struct nvme_tcp_queue* queue = (void*)c->queue;
    void* hdr = BPF_CORE_READ(queue, pdu);
    u8 type = 0;

    bpf_probe_read_kernel(&type, sizeof(type), hdr);
    if (type == nvme_tcp_c2h_data) rec->hdr.flags |= NTP_F_DATA_SUCCESS;
  }
  return 0;
}

SEC("fentry/blk_mq_complete_request_remote")
int BPF_PROG(ntp_h_complete_fe, struct request* rq) { return on_complete(rq); }

SEC("kprobe/blk_mq_complete_request_remote")
int BPF_KPROBE(ntp_h_complete_kp, struct request* rq) { return on_complete(rq); }
