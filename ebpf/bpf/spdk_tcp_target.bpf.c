// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP target in SPDK (lib/nvmf/tcp.c with the ntprof USDT
 * probes). The backend is SPDK's bdev layer and the device under it; it has
 * no block-layer split, so NTP_T_BE_ISSUE / NTP_T_BE_DONE stay unset.
 *
 *   nvmf_tcp_cmd                    -> NTP_T_CMD_ARR, NTP_T_CMD_PROC
 *   nvmf_tcp_pdu R2T                -> NTP_T_R2T_QUEUED
 *   sock_req_sent (R2T)             -> NTP_T_R2T_SENT
 *   nvmf_tcp_h2c                    -> NTP_T_DATA_ARR, NTP_T_DATA_PROC
 *   nvmf_tcp_state EXECUTING        -> NTP_T_EXEC
 *   nvmf_tcp_state EXECUTED         -> NTP_T_COMPLETE
 *   nvmf_tcp_pdu C2H data / response -> NTP_T_RSP_QUEUED
 *   sock_req_sent (C2H data)        -> NTP_T_C2H_SENT (the first one), and
 *                                      NTP_T_RSP_SENT with the SUCCESS flag
 *   sock_req_sent (response)        -> NTP_T_RSP_SENT
 *
 * The record is emitted with NTP_T_RSP_SENT, before the host can complete
 * the command (state COMPLETED comes only when the send's buffers are
 * released, after the zero-copy notification); COMPLETED emits the records
 * of commands that ended without a response.
 *
 * As in v1 and the kernel target, a PDU counts as sent when it is handed to
 * TCP: the entry of the tcp_sendmsg() call that carried it. sock_req_sent
 * fires right after that call returns, on the same thread. (The initiator,
 * as v1's done_send_req, counts the return.)
 */
#include "spdk.bpf.h"

/* enum spdk_nvmf_tcp_req_state (lib/nvmf/tcp.c; explicit values upstream) */
#define SPDK_TCP_REQ_EXECUTING 9
#define SPDK_TCP_REQ_EXECUTED 11
#define SPDK_TCP_REQ_COMPLETED 15

struct st_entry {
  u64 req;          /* struct spdk_nvmf_tcp_req (user-space address); 0 = free */
  u8 pending;       /* type of the PDU last queued */
  u8 pending_flags; /* and its flags */
  u8 pad[6];
  struct ntp_tgt_rec rec; /* emitted as is */
};

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct st_entry);
} streqs SEC(".maps");

static __always_inline struct st_entry* st_entry(u64 req) {
  u32 slot = spdk_slot(req);
  struct st_entry* e = bpf_map_lookup_elem(&streqs, &slot);

  return (e && e->req == req) ? e : NULL;
}

static __always_inline void st_finish(struct st_entry* e) {
  e->rec.hdr.cpu = bpf_get_smp_processor_id();
  spdk_emit(&e->rec, sizeof(e->rec), NTP_CNT_TGT_EMITTED);
  e->req = 0;
}

/* ---- the start of the SPDK thread's latest tcp_sendmsg() ---- */

struct spdk_tx {
  u64 tid;
  u64 entry;
};
NTP_CPU_CTX(spdk_tx, struct spdk_tx);

static __always_inline int spdk_sendmsg(void) {
  u32 zero = 0;
  struct spdk_tx* t;

  if (!filter.npids || !spdk_process()) return 0;
  t = bpf_map_lookup_elem(&spdk_tx, &zero);
  if (!t) return 0;
  t->tid = ntp_tid();
  t->entry = bpf_ktime_get_ns();
  return 0;
}

SEC("fentry/tcp_sendmsg")
int BPF_PROG(ntp_s_sendmsg_fe) { return spdk_sendmsg(); }

SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(ntp_s_sendmsg_kp) { return spdk_sendmsg(); }

/* When the PDU sock_req_sent reports was handed to TCP, or @now. */
static __always_inline u64 spdk_handed_to_tcp(u64 now) {
  struct spdk_tx* t = NTP_CTX_GET(spdk_tx);

  return (t && t->entry && t->entry <= now) ? t->entry : now;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_tcp_cmd, u64 req, u64 cmd, u32 qid, u32 cntlid) {
  struct ntp_cmd_id id = {};
  struct ntp_tgt_rec* rec;
  struct st_entry* e;
  u64 now = bpf_ktime_get_ns();
  u32 bytes, slot;
  bool write;

  if (!ntp_armed || !req) return 0;
  if (spdk_read_cmd(cmd, &id, &bytes) || !ntp_is_rw(id.opcode)) return 0;
  /* SPDK reports sampled commands only: the count is an estimate */
  ntp_count(NTP_CNT_TGT_CMDS, filter.sample_every);
  write = id.opcode == NTP_NVME_CMD_WRITE;
  if (!spdk_selected(&id, bytes, write)) return 0;

  slot = spdk_slot(req);
  e = bpf_map_lookup_elem(&streqs, &slot);
  if (!e) return 0;
  if (e->req)
    ntp_count(NTP_CNT_STALE, 1);
  else
    ntp_inflight_add(1);
  e->req = 0;
  e->pending = e->pending_flags = 0;
  __builtin_memset(&e->rec, 0, sizeof(e->rec));
  rec = &e->rec;
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;
  rec->hdr.kind = NTP_REC_TARGET;
  rec->hdr.transport = NTP_TR_TCP;
  rec->hdr.flags = NTP_F_SPDK | (write ? NTP_F_WRITE : 0);
  rec->ts[NTP_T_CMD_ARR] = spdk_arrival(now);
  rec->ts[NTP_T_CMD_PROC] = now;
  e->req = req; /* last: the slot is ours from here on */
  ntp_count(NTP_CNT_TGT_SAMPLED, 1);
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_tcp_pdu, u64 req, u32 type, u32 flags) {
  struct st_entry* e = st_entry(req);
  __u64* T;

  if (!e) return 0;
  T = e->rec.ts;
  e->pending = type;
  e->pending_flags = flags;
  if (type == NTP_PDU_R2T) {
    if (T[NTP_T_R2T_QUEUED])
      e->rec.hdr.flags |= NTP_F_MULTI_R2T;
    else
      T[NTP_T_R2T_QUEUED] = bpf_ktime_get_ns();
    e->rec.hdr.flags |= NTP_F_R2T;
  } else if (type == NTP_PDU_C2H_DATA || type == NTP_PDU_RSP) {
    ntp_stamp_once(&T[NTP_T_RSP_QUEUED], bpf_ktime_get_ns());
  }
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_sock_req_sent, u64 req) {
  struct st_entry* e = st_entry(req);
  __u64* T;
  u64 now;

  if (!e) return 0;
  now = spdk_handed_to_tcp(bpf_ktime_get_ns());
  T = e->rec.ts;
  switch (e->pending) {
    case NTP_PDU_R2T:
      ntp_stamp_once(&T[NTP_T_R2T_SENT], now);
      break;
    case NTP_PDU_C2H_DATA:
      ntp_stamp_once(&T[NTP_T_C2H_SENT], now);
      if (e->pending_flags & NVME_TCP_F_DATA_SUCCESS) {
        T[NTP_T_RSP_SENT] = now;
        e->rec.hdr.flags |= NTP_F_DATA_SUCCESS;
        st_finish(e);
      }
      break;
    case NTP_PDU_RSP:
      T[NTP_T_RSP_SENT] = now;
      st_finish(e);
      break;
  }
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_tcp_h2c, u64 req) {
  struct st_entry* e = st_entry(req);
  u64 now = bpf_ktime_get_ns();

  if (!e || e->rec.ts[NTP_T_DATA_PROC]) return 0;
  e->rec.ts[NTP_T_DATA_ARR] = spdk_arrival(now);
  e->rec.ts[NTP_T_DATA_PROC] = now;
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_tcp_state, u64 req, u32 state) {
  struct st_entry* e = st_entry(req);
  u64 now = bpf_ktime_get_ns();

  if (!e) return 0;
  switch (state) {
    case SPDK_TCP_REQ_EXECUTING:
      ntp_stamp_once(&e->rec.ts[NTP_T_EXEC], now);
      break;
    case SPDK_TCP_REQ_EXECUTED:
      ntp_stamp_once(&e->rec.ts[NTP_T_COMPLETE], now);
      break;
    case SPDK_TCP_REQ_COMPLETED:
      st_finish(e);
      break;
  }
  return 0;
}
