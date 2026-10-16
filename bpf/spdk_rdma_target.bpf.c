// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/RDMA target in SPDK (lib/nvmf/rdma.c with ntprof's hooks).
 * The backend is SPDK's bdev layer and the device under it; it has no
 * block-layer split, so NTP_T_BE_ISSUE / NTP_T_BE_DONE stay unset.
 *
 * SPDK's times (spdk.bpf.h) -> record (the kernel nvmet-rdma object's indices):
 *   CMD_ARR                 -> NTP_T_CMD_ARR (the command's completion polled)
 *   CMD                     -> NTP_T_CMD_PROC
 *   READ_QUEUED             -> NTP_T_R2T_QUEUED (a write's RDMA READ queued)
 *   READ_POSTED             -> NTP_T_R2T_SENT (... posted)
 *   READ_DONE               -> NTP_T_DATA_ARR, NTP_T_DATA_PROC (its completion)
 *   EXEC, EXECUTED          -> NTP_T_EXEC, NTP_T_COMPLETE
 *   RSP_QUEUED              -> NTP_T_RSP_QUEUED
 *   RSP_POSTED              -> NTP_T_RSP_SENT, and for a read NTP_T_C2H_SENT
 *                              (its RDMA WRITE is posted with the response)
 *   SEND_DONE               -> NTP_T_SEND_DONE (the response's send completion)
 */
#include "spdk.bpf.h"

SEC("usdt")
int BPF_USDT(ntp_u_nvmf_rdma_done, u64 req, u64 cmd, u32 qid, u32 cntlid,
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
  bytes = st.bytes;
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
  rec->hdr.transport = NTP_TR_RDMA;
  rec->hdr.cpu = bpf_get_smp_processor_id();
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;

  T = rec->ts;
  T[NTP_T_CMD_ARR] = st.t[SPDK_RT_CMD_ARR];
  T[NTP_T_CMD_PROC] = st.t[SPDK_RT_CMD];
  T[NTP_T_R2T_QUEUED] = st.t[SPDK_RT_READ_QUEUED];
  T[NTP_T_R2T_SENT] = st.t[SPDK_RT_READ_POSTED];
  T[NTP_T_DATA_ARR] = st.t[SPDK_RT_READ_DONE];
  T[NTP_T_DATA_PROC] = st.t[SPDK_RT_READ_DONE];
  T[NTP_T_EXEC] = st.t[SPDK_RT_EXEC];
  T[NTP_T_COMPLETE] = st.t[SPDK_RT_EXECUTED];
  T[NTP_T_RSP_QUEUED] = st.t[SPDK_RT_RSP_QUEUED];
  T[NTP_T_RSP_SENT] = st.t[SPDK_RT_RSP_POSTED];
  if (!write) T[NTP_T_C2H_SENT] = st.t[SPDK_RT_RSP_POSTED];
  T[NTP_T_SEND_DONE] = st.t[SPDK_RT_SEND_DONE];

  flags = NTP_F_SPDK;
  if (write) {
    flags |= NTP_F_WRITE;
    /* fetched with RDMA READ, or carried in the command capsule */
    flags |= st.t[SPDK_RT_READ_QUEUED] ? NTP_F_RDMA_READ : NTP_F_INLINE_DATA;
  }
  rec->hdr.flags = flags;
  spdk_submit(rec, NTP_CNT_TGT_EMITTED);
  return 0;
}
