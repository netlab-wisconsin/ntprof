// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/TCP target side (drivers/nvme/target/tcp.c, unmodified).
 *
 * The record and the nvmet-core / backend / block-layer hooks are in
 * target.bpf.h; the NVMe/TCP hooks here:
 *
 *   skb_copy_datagram_iter             -> the last skb this thread copied, and
 *                                         its socket (arrival of what is parsed)
 *   skb_copy_datagram_iter exit        -> a write's time copying its data,
 *                                         NTP_T_DATA_DONE / NTP_T_DATA_LAST_ARR
 *   nvmet_tcp_queue_response           -> NTP_T_R2T_QUEUED / NTP_T_RSP_QUEUED
 *   nvmet_tcp_build_pdu_iovec          -> NTP_T_DATA_* (solicited H2C data)
 *   sock_sendmsg, kernel_sendpage      -> NTP_T_R2T_SENT / C2H_SENT / RSP_SENT
 *
 * nvmet-tcp stores its queue in the socket's sk_user_data, which ties a
 * command (its nvmet_sq is in the queue), a socket and a send together without
 * a hook on nvmet_tcp_io_work. Only the copy hook runs for every received
 * skb; the others return at once while no sampled command is in flight.
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

/* The queue a target nvmet_sq is embedded in. */
static __always_inline struct nvmet_tcp_queue* queue_of_sq(const struct nvmet_sq* sq) {
  return (void*)sq - bpf_core_field_offset(struct nvmet_tcp_queue, nvme_sq);
}

/* ---- what this thread last read from a socket ---- */

struct tcp_rx {
  u64 tid;
  u64 skb;    /* the last skb copied */
  u64 sk;     /* its socket */
  u64 enter;  /* when that copy started; 0 while no record is in flight */
  s64 offset; /* where in the skb's data it started */
};
NTP_CPU_CTX(tcp_rx, struct tcp_rx);

/*
 * kernel_recvmsg() -> tcp_recvmsg() copies each skb with this helper; the
 * command or data being parsed next came in the last one copied. Its arrival
 * is looked up only for sampled commands.
 */
static __always_inline int on_copy(const struct sk_buff* skb, int offset) {
  u32 zero = 0;
  struct tcp_rx* r = bpf_map_lookup_elem(&tcp_rx, &zero);

  if (!r) return 0;
  r->tid = ntp_tid();
  r->skb = (u64)skb;
  r->sk = (u64)BPF_CORE_READ(skb, sk);
  r->enter = ntp_inflight ? bpf_ktime_get_ns() : 0;
  r->offset = offset;
  return 0;
}

SEC("fentry/skb_copy_datagram_iter")
int BPF_PROG(ntp_t_copy_fe, const struct sk_buff* skb, int offset) {
  return on_copy(skb, offset);
}

SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_t_copy_kp, const struct sk_buff* skb, int offset) {
  return on_copy(skb, offset);
}

/*
 * A copy is over. If it moved data of a sampled write solicited by R2T into
 * the command's buffer, its time counts as the transport's processing of the
 * write's data (model.c: the rest of the data phase is the wait for data).
 * nvmet-tcp receives the data with sock_recvmsg() in functions inlined into
 * nvmet_tcp_io_work() on every kernel checked (5.15 with MLNX OFED, 6.8,
 * 6.11), so the copies are what is left to time. TCP's own work between them
 * (freeing skbs, ACKs, the socket backlog) is not counted, as on the host,
 * where nvme_tcp_recv_skb() runs between the same steps of tcp_read_sock().
 */
static __always_inline int on_copy_done(int ret) {
  struct nvmet_tcp_queue* queue;
  struct nvmet_tcp_cmd* cmd;
  struct ntp_tgt_rec* rec;
  struct tgt_entry* e;
  struct tcp_rx* r;
  u64 enter, from, now;

  if (!ntp_inflight) return 0;
  r = NTP_CTX_GET(tcp_rx);
  if (!r) return 0;
  enter = r->enter;
  r->enter = 0;
  if (!enter || ret) return 0;

  /* nvmet-tcp's socket: the queue in sk_user_data points back to it */
  queue = BPF_CORE_READ((struct sock*)r->sk, sk_user_data);
  if (!queue || (u64)BPF_CORE_READ(queue, sock, sk) != r->sk) return 0;
  /* receiving a command's data (not a PDU header or a digest) */
  if (BPF_CORE_READ(queue, rcv_state) != NVMET_TCP_RECV_DATA) return 0;
  cmd = BPF_CORE_READ(queue, cmd);
  if (!cmd) return 0;
  e = tgt_entry(req_of(cmd));
  if (!e) return 0;
  rec = &e->rec;
  /* data after an R2T, its PDU header parsed (on_h2c), not yet executed */
  if (!(rec->hdr.flags & NTP_F_R2T) || !rec->ts[NTP_T_DATA_PROC] || rec->ts[NTP_T_EXEC])
    return 0;

  now = bpf_ktime_get_ns();
  /*
   * The first copy of the data counts from the header's parsing when it
   * starts inside an skb: the PDU header came in the same skb, so the data
   * was already here and the transport has been working on it since.
   */
  from = (!rec->h2c_skbs && r->offset > 0) ? rec->ts[NTP_T_DATA_PROC] : enter;
  if (now < from) return 0;
  rec->h2c_busy_ns += now - from;
  rec->h2c_skbs += 1;
  rec->ts[NTP_T_DATA_DONE] = now;
  rec->ts[NTP_T_DATA_LAST_ARR] = ntp_skb_arrival((void*)r->skb, now);
  return 0;
}

SEC("fexit/skb_copy_datagram_iter")
int BPF_PROG(ntp_t_copy_done_fx, const struct sk_buff* skb, int offset, void* to, int len,
             int ret) {
  return on_copy_done(ret);
}

SEC("kretprobe/skb_copy_datagram_iter")
int BPF_KRETPROBE(ntp_t_copy_done_kr, int ret) { return on_copy_done(ret); }

/* When what this thread parses from socket @sk arrived, or @now. */
static __always_inline u64 rx_arrival(u64 sk, u64 now) {
  struct tcp_rx* r = NTP_CTX_GET(tcp_rx);

  if (!r || r->sk != sk) return now;
  return ntp_skb_arrival((void*)r->skb, now);
}

/* ---- command arrival (target.bpf.h) ---- */

/*
 * Queues whose sends sock_sendmsg must look at, queue -> its socket: those
 * with a sampled command (or every queue when queues are counted exactly).
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 4096);
  __type(key, u64);
  __type(value, u64);
} tqueues SEC(".maps");

static __always_inline void remember_queue(struct nvmet_tcp_queue* queue) {
  u64 key = (u64)queue;
  u64 sock = (u64)BPF_CORE_READ(queue, sock);

  bpf_map_update_elem(&tqueues, &key, &sock, BPF_ANY);
}

/*
 * An nvmet_sq of nvmet-tcp sits in a queue whose socket points back to it
 * (nvmet_tcp_set_queue_sock()); for another transport the same offsets hold
 * unrelated data and the test fails.
 */
static __always_inline bool tgt_is_ours(struct nvmet_req* req, struct nvmet_sq* sq) {
  struct nvmet_tcp_queue* queue = queue_of_sq(sq);

  if ((u64)BPF_CORE_READ(queue, sock, sk, sk_user_data) != (u64)queue) return false;
  if (filter.track_queues) remember_queue(queue);
  return true;
}

static __always_inline u64 tgt_sampled(struct nvmet_req* req, struct nvmet_sq* sq,
                                       u64 now) {
  struct nvmet_tcp_queue* queue = queue_of_sq(sq);

  remember_queue(queue);
  return rx_arrival((u64)BPF_CORE_READ(queue, sock, sk), now);
}

/* ---- transport events ---- */

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
  rec->hdr.flags |= NTP_F_R2T;
  rec->ts[NTP_T_DATA_ARR] = rx_arrival((u64)BPF_CORE_READ(cmd, queue, sock, sk), now);
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
  u64 key, req, now, *qsock;

  if (ntp_idle()) return 0;
  /* a queue with a sampled command, sending on its own socket */
  queue = BPF_CORE_READ(sock, sk, sk_user_data);
  key = (u64)queue;
  qsock = bpf_map_lookup_elem(&tqueues, &key);
  if (!qsock || *qsock != ntp_scalar((u64)sock)) return 0;
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

/*
 * Up to Linux 6.4 (and in MLNX OFED's backport for 5.15) nvmet-tcp sends its
 * PDUs with kernel_sendpage(), which does not go through sock_sendmsg; pages
 * that cannot be sent that way go through sock_no_sendpage() ->
 * sock_sendmsg() above. A send takes one of the two paths, never both.
 */
SEC("fentry/kernel_sendpage")
int BPF_PROG(ntp_t_sendpage_fe, struct socket* sock) { return on_sendmsg(sock); }

SEC("kprobe/kernel_sendpage")
int BPF_KPROBE(ntp_t_sendpage_kp, struct socket* sock) { return on_sendmsg(sock); }
