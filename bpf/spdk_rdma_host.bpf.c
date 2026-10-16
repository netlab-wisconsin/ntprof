// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof, NVMe/RDMA host in SPDK (lib/nvme/nvme_rdma.c with ntprof's
 * hooks). As over TCP, SPDK has no block layer: a command starts when the
 * application submits it to the transport and ends when the transport
 * completes it to the application (S1 is empty at both ends). As in the
 * kernel's nvme-rdma, the host sees one round trip: the data moves by the
 * target's RDMA READ or WRITE without it.
 *
 * SPDK's times (spdk.bpf.h) -> record (the kernel nvme-rdma object's indices):
 *   SUBMIT                  -> NTP_H_START, NTP_H_QUEUE_RQ
 *   CMD_POSTED              -> NTP_H_CMD_SENT (the command's SEND posted)
 *   SEND_DONE               -> NTP_H_SEND_DONE (its send completion polled)
 *   RSP                     -> NTP_H_RSP_ARR (the response's completion polled)
 *   DONE                    -> NTP_H_RSP_PROC, NTP_H_NVME_DONE, NTP_H_BLK_DONE
 */
#include "spdk.bpf.h"

SEC("usdt")
int BPF_USDT(ntp_u_nvme_rdma_done, u64 req, u64 cmd, u32 qid, u32 cntlid,
             u64 times) {
  struct ntp_cmd_id id = {};
  struct ntp_host_rec* rec;
  struct spdk_times st;
  __u64* H;
  u32 bytes, flags;
  bool write;

  if (!ntp_armed) return 0;
  if (spdk_read_cmd(cmd, &id, &bytes) || !ntp_is_rw(id.opcode)) return 0;
  if (bpf_probe_read_user(&st, sizeof(st), (const void*)times)) return 0;
  /* a keyed SGL holds 24 bits of the length: SPDK passes it */
  bytes = st.bytes;
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
  rec->hdr.transport = NTP_TR_RDMA;
  rec->hdr.cpu = bpf_get_smp_processor_id();
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;
  rec->ctrl_instance = cntlid;

  H = rec->ts;
  H[NTP_H_START] = st.t[SPDK_RH_SUBMIT];
  H[NTP_H_QUEUE_RQ] = st.t[SPDK_RH_SUBMIT];
  H[NTP_H_CMD_SENT] = st.t[SPDK_RH_CMD_POSTED];
  H[NTP_H_SEND_DONE] = st.t[SPDK_RH_SEND_DONE];
  H[NTP_H_RSP_ARR] = st.t[SPDK_RH_RSP];
  H[NTP_H_RSP_PROC] = st.t[SPDK_RH_DONE];
  H[NTP_H_NVME_DONE] = st.t[SPDK_RH_DONE];
  H[NTP_H_BLK_DONE] = st.t[SPDK_RH_DONE];

  /* as the kernel's nvme-rdma object: the target knows how the data moved */
  flags = NTP_F_SPDK;
  if (write) flags |= NTP_F_WRITE;
  rec->hdr.flags = flags;
  /* the session: controller and namespace, as nvmeXcYnZ names them */
  BPF_SNPRINTF(rec->disk, sizeof(rec->disk), "spdk-c%un%u", cntlid, id.nsid);
  spdk_submit(rec, NTP_CNT_HOST_EMITTED);
  return 0;
}
