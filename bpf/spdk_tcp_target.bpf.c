// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/TCP target in SPDK (lib/nvmf/tcp.c with ntprof's hooks).
 * The backend is SPDK's bdev layer and the device under it; it has no
 * block-layer split, so NTP_T_BE_ISSUE / NTP_T_BE_DONE stay unset.
 *
 * SPDK's times (spdk.bpf.h) -> record:
 *   CMD                     -> NTP_T_CMD_PROC; its arrival -> NTP_T_CMD_ARR
 *   R2T_QUEUED              -> NTP_T_R2T_QUEUED
 *   R2T_SENT                -> NTP_T_R2T_SENT
 *   H2C                     -> NTP_T_DATA_PROC; its arrival -> NTP_T_DATA_ARR
 *   EXEC, EXECUTED          -> NTP_T_EXEC, NTP_T_COMPLETE
 *   RSP_QUEUED              -> NTP_T_RSP_QUEUED
 *   C2H_SENT, RSP_SENT      -> NTP_T_C2H_SENT, NTP_T_RSP_SENT
 * "Sent" is, as in the kernel target, when the PDU was handed to TCP:
 * the entry of the tcp_sendmsg() that SPDK's socket layer saw take it.
 *
 * The record is emitted when the request completes, which with zero-copy
 * send waits for the kernel's notification; a host record of the same
 * machine can come first, and waits in the joiner.
 */
#include "spdk.bpf.h"

/* ---- the entries of this SPDK thread's tcp_sendmsg() calls ---- */

static __always_inline int spdk_sendmsg(void) {
  struct spdk_ring* r;
  u64 now;

  if (!filter.npids || !spdk_process()) return 0;
  r = spdk_ring_own(&spdk_send_ring);
  if (!r) return 0;
  now = bpf_ktime_get_ns();
  spdk_ring_add(r, now, now);
  return 0;
}

SEC("fentry/tcp_sendmsg")
int BPF_PROG(ntp_s_sendmsg_fe) { return spdk_sendmsg(); }

SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(ntp_s_sendmsg_kp) { return spdk_sendmsg(); }

/*
 * When the PDU sendmsg() took at @taken (0: not sent) had been handed to TCP,
 * not before it was queued at @queued; @taken itself when unknown.
 */
static __always_inline u64 spdk_handed(u64 taken, u64 queued) {
  u64 e = spdk_ring_find(1, taken);

  return (e && e >= queued && e <= taken) ? e : taken;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_tcp_done, u64 req, u64 cmd, u32 qid, u32 cntlid,
             u64 times) {
  struct ntp_cmd_id id = {};
  struct ntp_tgt_rec* rec;
  struct spdk_times st;
  __u64* T;
  u32 bytes, flags;
  bool write;

  if (!ntp_armed) return 0;
  if (spdk_read_cmd(cmd, &id, &bytes) || !ntp_is_rw(id.opcode)) return 0;
  if (bpf_probe_read_user(&st, sizeof(st), (const void*)times)) return 0;
  /* SPDK reports sampled commands only: the count is an estimate */
  ntp_count(NTP_CNT_TGT_CMDS, filter.sample_every);
  write = id.opcode == NTP_NVME_CMD_WRITE;
  if (!spdk_selected(&id, bytes, write)) return 0;
  ntp_count(NTP_CNT_TGT_SAMPLED, 1);

  rec = bpf_ringbuf_reserve(&events, sizeof(*rec), 0);
  if (!rec) {
    ntp_count(NTP_CNT_RB_DROPS, 1);
    return 0;
  }
  __builtin_memset(rec, 0, sizeof(*rec));
  rec->hdr.kind = NTP_REC_TARGET;
  rec->hdr.transport = NTP_TR_TCP;
  rec->hdr.cpu = bpf_get_smp_processor_id();
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;

  T = rec->ts;
  T[NTP_T_CMD_PROC] = st.t[SPDK_T_CMD];
  T[NTP_T_CMD_ARR] = spdk_arrival(st.t[SPDK_T_CMD], 0);
  T[NTP_T_R2T_QUEUED] = st.t[SPDK_T_R2T_QUEUED];
  T[NTP_T_R2T_SENT] = spdk_handed(st.t[SPDK_T_R2T_SENT], st.t[SPDK_T_R2T_QUEUED]);
  T[NTP_T_DATA_PROC] = st.t[SPDK_T_H2C];
  T[NTP_T_DATA_ARR] = spdk_arrival(st.t[SPDK_T_H2C], st.t[SPDK_T_R2T_QUEUED]);
  T[NTP_T_EXEC] = st.t[SPDK_T_EXEC];
  T[NTP_T_COMPLETE] = st.t[SPDK_T_EXECUTED];
  T[NTP_T_RSP_QUEUED] = st.t[SPDK_T_RSP_QUEUED];
  T[NTP_T_C2H_SENT] = spdk_handed(st.t[SPDK_T_C2H_SENT], st.t[SPDK_T_RSP_QUEUED]);
  T[NTP_T_RSP_SENT] = spdk_handed(st.t[SPDK_T_RSP_SENT], st.t[SPDK_T_RSP_QUEUED]);

  flags = NTP_F_SPDK;
  if (write) flags |= NTP_F_WRITE;
  if (st.t[SPDK_T_R2T_QUEUED]) flags |= NTP_F_R2T;
  if (st.r2ts > 1) flags |= NTP_F_MULTI_R2T;
  if (st.flags & SPDK_F_C2H_SUCCESS) flags |= NTP_F_DATA_SUCCESS;
  rec->hdr.flags = flags;
  spdk_submit(rec, NTP_CNT_TGT_EMITTED);
  return 0;
}
