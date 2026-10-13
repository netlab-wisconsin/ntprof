// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP target side (drivers/nvme/target/tcp.c, unmodified).
 *
 * The record and the nvmet-core / backend / block-layer hooks are in
 * target.bpf.h; the NVMe/TCP hooks here:
 *
 *   nvmet_tcp_io_work entry / exit     -> context: the queue this thread serves
 *   skb_copy_datagram_iter             -> the skb being read (its arrival)
 *   nvmet_tcp_queue_response           -> NTP_T_R2T_QUEUED / NTP_T_RSP_QUEUED
 *   nvmet_tcp_build_pdu_iovec          -> NTP_T_DATA_* (solicited H2C data)
 *   sock_sendmsg                       -> NTP_T_R2T_SENT / C2H_SENT / RSP_SENT
 *
 * Function hooks exist as fentry/fexit and as kprobe/kretprobe programs with
 * the same body (common.bpf.h).
 */
#define NTP_TRANSPORT NTP_TR_TCP
#include "target.bpf.h"

/* container_of() in both directions between nvmet_tcp_cmd and its nvmet_req */
static __always_inline struct nvmet_tcp_cmd* cmd_of(const struct nvmet_req* req) {
  return (void*)req - bpf_core_field_offset(struct nvmet_tcp_cmd, req);
}

static __always_inline u64 req_of(const struct nvmet_tcp_cmd* cmd) {
  return ntp_scalar((u64)cmd + bpf_core_field_offset(struct nvmet_tcp_cmd, req));
}

/* ---- context ---- */

static __always_inline int on_io_work(struct work_struct* w) {
  struct nvmet_tcp_queue* queue =
      (void*)w - bpf_core_field_offset(struct nvmet_tcp_queue, io_work);

  tgt_ctx_enter((u64)queue, (u64)BPF_CORE_READ(queue, sock, sk), 0);
  return 0;
}

SEC("fentry/nvmet_tcp_io_work")
int BPF_PROG(ntp_t_io_work_fe, struct work_struct* w) { return on_io_work(w); }

SEC("fexit/nvmet_tcp_io_work")
int BPF_PROG(ntp_t_io_work_fx) { return tgt_ctx_exit(); }

SEC("kprobe/nvmet_tcp_io_work")
int BPF_KPROBE(ntp_t_io_work_kp, struct work_struct* w) { return on_io_work(w); }

SEC("kretprobe/nvmet_tcp_io_work")
int BPF_KRETPROBE(ntp_t_io_work_kr) { return tgt_ctx_exit(); }

/*
 * kernel_recvmsg() -> tcp_recvmsg() copies each skb with this helper. Inside
 * nvmet_tcp_io_work only the queue's own socket is read. The arrival time is
 * looked up only for sampled commands (tgt_arrival()).
 */
static __always_inline int on_copy(const struct sk_buff* skb) {
  struct tgt_ctx* c = NTP_CTX_GET(tgt_ctx);

  if (c) c->last_skb = (u64)skb;
  return 0;
}

SEC("fentry/skb_copy_datagram_iter")
int BPF_PROG(ntp_t_copy_fe, const struct sk_buff* skb) { return on_copy(skb); }

SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_t_copy_kp, const struct sk_buff* skb) { return on_copy(skb); }

/*
 * ->queue_response() is used twice for a write without in-capsule data:
 * first to send the R2T (data still to come), then for the completion.
 */
static __always_inline int on_queue_resp(struct nvmet_req* req) {
  struct ntp_tgt_rec* rec;
  u64 key, now;

  if (ntp_idle()) return 0;
  key = ntp_scalar((u64)req);
  rec = tgt_rec(key);

  if (filter.track_queues || rec) {
    struct nvmet_tcp_cmd* cmd = cmd_of(req);
    struct ntp_nvme_cmd* nc = BPF_CORE_READ(req, cmd);
    u8 opcode = 0;

    bpf_probe_read_kernel(&opcode, sizeof(opcode), &nc->opcode);
    /* nvmet_tcp_need_data_in(): a write whose data has not all arrived */
    if (opcode == NTP_NVME_CMD_WRITE &&
        BPF_CORE_READ(cmd, rbytes_done) < BPF_CORE_READ(req, transfer_len)) {
      if (rec) ntp_stamp_once(&rec->ts[NTP_T_R2T_QUEUED], bpf_ktime_get_ns());
      return 0;
    }
  }
  if (filter.track_queues) count_resp_queued(key);
  if (rec) {
    now = bpf_ktime_get_ns();
    ntp_stamp_once(&rec->ts[NTP_T_RSP_QUEUED], now);
  }
  return 0;
}

SEC("fentry/nvmet_tcp_queue_response")
int BPF_PROG(ntp_t_queue_resp_fe, struct nvmet_req* req) {
  return on_queue_resp(req);
}

SEC("kprobe/nvmet_tcp_queue_response")
int BPF_KPROBE(ntp_t_queue_resp_kp, struct nvmet_req* req) {
  return on_queue_resp(req);
}

/* Solicited data: called once per H2C data PDU (and for in-capsule data). */
static __always_inline int on_h2c(struct nvmet_tcp_cmd* cmd) {
  struct ntp_tgt_rec* rec;
  struct tgt_ctx* c;
  u64 now;

  if (!ntp_inflight) return 0;
  rec = tgt_rec(req_of(cmd));
  if (!rec) return 0;
  if (!rec->ts[NTP_T_R2T_SENT]) {
    rec->hdr.flags |= NTP_F_INLINE_DATA;
    return 0;
  }
  if (rec->ts[NTP_T_DATA_PROC]) return 0;
  now = bpf_ktime_get_ns();
  c = NTP_CTX_GET(tgt_ctx);
  rec->hdr.flags |= NTP_F_R2T;
  rec->ts[NTP_T_DATA_ARR] = c ? tgt_arrival(c, now) : now;
  rec->ts[NTP_T_DATA_PROC] = now;
  return 0;
}

SEC("fentry/nvmet_tcp_build_pdu_iovec")
int BPF_PROG(ntp_t_h2c_fe, struct nvmet_tcp_cmd* cmd) { return on_h2c(cmd); }

SEC("kprobe/nvmet_tcp_build_pdu_iovec")
int BPF_KPROBE(ntp_t_h2c_kp, struct nvmet_tcp_cmd* cmd) { return on_h2c(cmd); }

/*
 * nvmet_tcp_try_send_one() sends queue->snd_cmd; cmd->state says which PDU
 * goes out, and cmd->offset == 0 marks its first byte (a PDU may need several
 * sendmsg calls).
 */
static __always_inline int on_sendmsg(struct socket* sock) {
  struct nvmet_tcp_queue* queue;
  struct nvmet_tcp_cmd* cmd;
  struct ntp_tgt_rec* rec;
  struct tgt_ctx* c;
  u64 req, now;

  if (ntp_idle()) return 0;
  c = NTP_CTX_GET(tgt_ctx);
  if (!c) return 0;
  queue = (void*)c->queue;
  /* a context left behind by a missed exit must not claim other sends */
  if ((u64)BPF_CORE_READ(queue, sock) != ntp_scalar((u64)sock)) return 0;
  cmd = BPF_CORE_READ(queue, snd_cmd);
  if (!cmd || BPF_CORE_READ(cmd, offset)) return 0;
  req = req_of(cmd);
  rec = tgt_rec(req);
  if (!rec && !filter.track_queues) return 0; /* unsampled: nothing to do */

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
        tgt_finish(req);
      }
      break;
    case NVMET_TCP_SEND_RESPONSE:
      if (rec) rec->ts[NTP_T_RSP_SENT] = now;
      tgt_finish(req);
      break;
    default:
      break;
  }
  return 0;
}

SEC("fentry/sock_sendmsg")
int BPF_PROG(ntp_t_sendmsg_fe, struct socket* sock) { return on_sendmsg(sock); }

SEC("kprobe/sock_sendmsg")
int BPF_KPROBE(ntp_t_sendmsg_kp, struct socket* sock) { return on_sendmsg(sock); }
