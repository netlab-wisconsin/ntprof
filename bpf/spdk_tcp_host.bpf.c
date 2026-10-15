// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/TCP host in SPDK (lib/nvme/nvme_tcp.c with ntprof's
 * hooks). SPDK has no block layer: a command starts when the application
 * submits it to the transport and ends when the transport completes it to the
 * application, which plays the part of blk_mq_complete_request_remote() (so
 * S1 is empty at both ends).
 *
 * SPDK's times (spdk.bpf.h) -> record:
 *   SUBMIT                  -> NTP_H_START, NTP_H_QUEUE_RQ
 *   CMD_SENT                -> NTP_H_CMD_SENT (sendmsg() took the capsule)
 *   R2T                     -> NTP_H_R2T_PROC; its arrival -> NTP_H_R2T_ARR
 *   H2C_START, H2C_SENT     -> NTP_H_DATA_START, NTP_H_DATA_SENT
 *   C2H                     -> NTP_H_C2H_PROC; its arrival -> NTP_H_C2H_ARR
 *   RSP (its arrival)       -> NTP_H_RSP_ARR
 *   DONE                    -> NTP_H_RSP_PROC, NTP_H_NVME_DONE, NTP_H_BLK_DONE
 */
#include "spdk.bpf.h"

SEC("usdt")
int BPF_USDT(ntp_u_nvme_tcp_done, u64 req, u64 cmd, u32 qid, u32 cntlid,
             u64 times) {
  struct ntp_cmd_id id = {};
  struct ntp_host_rec* rec;
  struct spdk_times st;
  __u64* H;
  u32 bytes, flags;
  bool write;
  u64 floor;

  if (!ntp_armed) return 0;
  if (spdk_read_cmd(cmd, &id, &bytes) || !ntp_is_rw(id.opcode)) return 0;
  if (bpf_probe_read_user(&st, sizeof(st), (const void*)times)) return 0;
  write = id.opcode == NTP_NVME_CMD_WRITE;
  /* SPDK reports sampled commands only: throughput is estimated from them */
  ntp_count(write ? NTP_CNT_HOST_WRITE_IOS : NTP_CNT_HOST_READ_IOS,
            filter.sample_every);
  ntp_count(write ? NTP_CNT_HOST_WRITE_BYTES : NTP_CNT_HOST_READ_BYTES,
            (u64)bytes * filter.sample_every);
  if (!spdk_selected(&id, bytes, write)) return 0;
  if (filter.nsid && id.nsid != filter.nsid) return 0;
  ntp_count(NTP_CNT_HOST_SAMPLED, 1);

  rec = bpf_ringbuf_reserve(&events, sizeof(*rec), 0);
  if (!rec) {
    ntp_count(NTP_CNT_RB_DROPS, 1);
    return 0;
  }
  __builtin_memset(rec, 0, sizeof(*rec));
  rec->hdr.kind = NTP_REC_HOST;
  rec->hdr.transport = NTP_TR_TCP;
  rec->hdr.cpu = bpf_get_smp_processor_id();
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;
  rec->ctrl_instance = cntlid;

  H = rec->ts;
  floor = st.t[SPDK_H_SUBMIT]; /* no answer arrives before the command left */
  H[NTP_H_START] = st.t[SPDK_H_SUBMIT];
  H[NTP_H_QUEUE_RQ] = st.t[SPDK_H_SUBMIT];
  H[NTP_H_CMD_SENT] = st.t[SPDK_H_CMD_SENT];
  H[NTP_H_R2T_PROC] = st.t[SPDK_H_R2T];
  H[NTP_H_R2T_ARR] = spdk_arrival(st.t[SPDK_H_R2T], floor);
  H[NTP_H_DATA_START] = st.t[SPDK_H_H2C_START];
  H[NTP_H_DATA_SENT] = st.t[SPDK_H_H2C_SENT];
  H[NTP_H_C2H_PROC] = st.t[SPDK_H_C2H];
  H[NTP_H_C2H_ARR] = spdk_arrival(st.t[SPDK_H_C2H], floor);
  H[NTP_H_RSP_ARR] = spdk_arrival(st.t[SPDK_H_RSP], floor);
  H[NTP_H_RSP_PROC] = st.t[SPDK_H_DONE];
  H[NTP_H_NVME_DONE] = st.t[SPDK_H_DONE];
  H[NTP_H_BLK_DONE] = st.t[SPDK_H_DONE];

  flags = NTP_F_SPDK;
  if (write) flags |= NTP_F_WRITE;
  if (st.t[SPDK_H_R2T]) flags |= NTP_F_R2T;
  if (st.r2ts > 1) flags |= NTP_F_MULTI_R2T;
  if (write && !st.t[SPDK_H_R2T]) flags |= NTP_F_INLINE_DATA;
  /* a read that ended with a C2H SUCCESS flag has no response capsule */
  if (!write && st.t[SPDK_H_C2H] && !st.t[SPDK_H_RSP]) flags |= NTP_F_DATA_SUCCESS;
  rec->hdr.flags = flags;
  /* the session: controller and namespace, as nvmeXcYnZ names them */
  BPF_SNPRINTF(rec->disk, sizeof(rec->disk), "spdk-c%un%u", cntlid, id.nsid);
  spdk_submit(rec, NTP_CNT_HOST_EMITTED);
  return 0;
}
