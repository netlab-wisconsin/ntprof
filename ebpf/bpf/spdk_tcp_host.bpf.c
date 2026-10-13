// SPDX-License-Identifier: GPL-2.0
/*
 * ntprof v2, NVMe/TCP initiator in SPDK (lib/nvme/nvme_tcp.c with the ntprof
 * USDT probes). SPDK has no block layer: a command starts when the
 * application submits it to the transport and ends when the transport
 * completes it to the application.
 *
 *   nvme_tcp_submit                 -> NTP_H_START, NTP_H_QUEUE_RQ
 *   nvme_tcp_pdu (capsule) and
 *   sock_req_sent                   -> NTP_H_CMD_SENT
 *   nvme_tcp_event R2T              -> NTP_H_R2T_ARR, NTP_H_R2T_PROC
 *   nvme_tcp_pdu (H2C data)         -> NTP_H_DATA_START (the first one)
 *   sock_req_sent (H2C data)        -> NTP_H_DATA_SENT (the last one)
 *   nvme_tcp_event C2H              -> NTP_H_C2H_ARR, NTP_H_C2H_PROC
 *   nvme_tcp_event RSP              -> NTP_H_RSP_ARR (the response's skb)
 *   nvme_tcp_event DONE             -> NTP_H_RSP_PROC, NTP_H_NVME_DONE,
 *                                      NTP_H_BLK_DONE
 *
 * NTP_H_RSP_PROC is where the transport hands the completion up, as
 * blk_mq_complete_request_remote() is in the kernel; in SPDK that is the
 * completion to the submitter, so S1 is empty at both ends.
 *
 * "Sent" is when SPDK's socket layer handed the whole PDU to the kernel
 * (sendmsg returned), not its write-completion callback, which waits for the
 * zero-copy notification when zero-copy send is on.
 */
#include "spdk.bpf.h"

struct sh_entry {
  u64 req;    /* struct nvme_tcp_req (user-space address); 0 = free */
  u64 pdu;    /* its struct nvme_tcp_pdu */
  u8 pending; /* type of the PDU last queued */
  u8 pad[7];
  struct ntp_host_rec rec; /* emitted as is */
};

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct sh_entry);
} sreqs SEC(".maps");

/* PDU -> request, for sock_req_sent (whose argument is the PDU) */
struct sh_pdu {
  u64 pdu;
  u64 req;
};

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NTP_REC_SLOTS);
  __type(key, u32);
  __type(value, struct sh_pdu);
} spdus SEC(".maps");

static __always_inline struct sh_entry* sh_entry(u64 req) {
  u32 slot = spdk_slot(req);
  struct sh_entry* e = bpf_map_lookup_elem(&sreqs, &slot);

  return (e && e->req == req) ? e : NULL;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvme_tcp_submit, u64 req, u64 cmd, u32 qid, u32 cntlid) {
  struct ntp_cmd_id id = {};
  struct ntp_host_rec* rec;
  struct sh_entry* e;
  u64 now = bpf_ktime_get_ns();
  u32 bytes, slot;
  bool write;

  if (!ntp_armed || !req) return 0;
  if (spdk_read_cmd(cmd, &id, &bytes) || !ntp_is_rw(id.opcode)) return 0;
  write = id.opcode == NTP_NVME_CMD_WRITE;
  /* SPDK reports sampled commands only: throughput is estimated from them */
  ntp_count(write ? NTP_CNT_HOST_WRITE_IOS : NTP_CNT_HOST_READ_IOS,
            filter.sample_every);
  ntp_count(write ? NTP_CNT_HOST_WRITE_BYTES : NTP_CNT_HOST_READ_BYTES,
            (u64)bytes * filter.sample_every);
  if (!spdk_selected(&id, bytes, write)) return 0;
  if (filter.nsid && id.nsid != filter.nsid) return 0;

  slot = spdk_slot(req);
  e = bpf_map_lookup_elem(&sreqs, &slot);
  if (!e) return 0;
  if (e->req)
    ntp_count(NTP_CNT_STALE, 1);
  else
    ntp_inflight_add(1);
  e->req = 0;
  e->pdu = 0;
  e->pending = 0;
  __builtin_memset(&e->rec, 0, sizeof(e->rec));
  rec = &e->rec;
  rec->id = id;
  rec->id.cntlid = cntlid;
  rec->id.qid = qid;
  rec->bytes = bytes;
  rec->ctrl_instance = cntlid;
  rec->hdr.kind = NTP_REC_HOST;
  rec->hdr.transport = NTP_TR_TCP;
  rec->hdr.flags = NTP_F_SPDK | (write ? NTP_F_WRITE : 0);
  rec->ts[NTP_H_START] = now;
  rec->ts[NTP_H_QUEUE_RQ] = now;
  e->req = req; /* last: the slot is ours from here on */
  ntp_count(NTP_CNT_HOST_SAMPLED, 1);
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvme_tcp_pdu, u64 req, u64 pdu, u32 type) {
  struct sh_entry* e = sh_entry(req);
  struct sh_pdu* p;
  u32 slot;

  if (!e) return 0;
  if (type == NTP_PDU_H2C_DATA)
    ntp_stamp_once(&e->rec.ts[NTP_H_DATA_START], bpf_ktime_get_ns());
  e->pending = type;
  if (e->pdu != pdu) {
    e->pdu = pdu;
    slot = spdk_slot(pdu);
    p = bpf_map_lookup_elem(&spdus, &slot);
    if (p) {
      p->pdu = pdu;
      p->req = req;
    }
  }
  return 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_sock_req_sent, u64 pdu) {
  u32 slot = spdk_slot(pdu);
  struct sh_pdu* p = bpf_map_lookup_elem(&spdus, &slot);
  struct sh_entry* e;
  u64 now = bpf_ktime_get_ns();

  if (!p || p->pdu != pdu) return 0;
  e = sh_entry(p->req);
  if (!e || e->pdu != pdu) return 0;
  if (e->pending == NTP_PDU_CMD)
    ntp_stamp_once(&e->rec.ts[NTP_H_CMD_SENT], now);
  else if (e->pending == NTP_PDU_H2C_DATA)
    e->rec.ts[NTP_H_DATA_SENT] = now;
  return 0;
}

static __always_inline void sh_finish(struct sh_entry* e, u64 now) {
  struct ntp_host_rec* rec = &e->rec;

  /* a read that ended with a C2H SUCCESS flag has no response capsule */
  if (!rec->ts[NTP_H_RSP_ARR] && rec->ts[NTP_H_C2H_PROC])
    rec->hdr.flags |= NTP_F_DATA_SUCCESS;
  rec->ts[NTP_H_RSP_PROC] = now;
  rec->ts[NTP_H_NVME_DONE] = now;
  rec->ts[NTP_H_BLK_DONE] = now;
  if ((rec->hdr.flags & NTP_F_WRITE) && !(rec->hdr.flags & NTP_F_R2T))
    rec->hdr.flags |= NTP_F_INLINE_DATA;
  /* the session: controller and namespace, as nvmeXcYnZ names them */
  BPF_SNPRINTF(rec->disk, sizeof(rec->disk), "spdk-c%un%u", rec->id.cntlid,
               rec->id.nsid);
  rec->hdr.cpu = bpf_get_smp_processor_id();
  spdk_emit(rec, sizeof(*rec), NTP_CNT_HOST_EMITTED);
  e->req = 0;
}

SEC("usdt")
int BPF_USDT(ntp_u_nvme_tcp_event, u64 req, u32 event) {
  struct sh_entry* e = sh_entry(req);
  u64 now = bpf_ktime_get_ns();
  __u64* H;

  if (!e) return 0;
  H = e->rec.ts;
  switch (event) {
    case NTP_SPDK_EV_C2H:
      if (!H[NTP_H_C2H_PROC]) {
        H[NTP_H_C2H_ARR] = spdk_arrival(now);
        H[NTP_H_C2H_PROC] = now;
      }
      break;
    case NTP_SPDK_EV_R2T:
      if (H[NTP_H_R2T_PROC]) {
        e->rec.hdr.flags |= NTP_F_MULTI_R2T;
      } else {
        H[NTP_H_R2T_ARR] = spdk_arrival(now);
        H[NTP_H_R2T_PROC] = now;
      }
      e->rec.hdr.flags |= NTP_F_R2T;
      break;
    case NTP_SPDK_EV_RSP:
      H[NTP_H_RSP_ARR] = spdk_arrival(now);
      break;
    case NTP_SPDK_EV_DONE:
      sh_finish(e, now);
      break;
  }
  return 0;
}
