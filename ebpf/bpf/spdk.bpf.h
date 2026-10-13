/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPDK's NVMe/TCP transports, seen through the USDT probes that
 * ebpf/spdk/ntprof-usdt-*.patch adds to SPDK (provider "ntprof").
 *
 * SPDK runs the NVMe/TCP transport in user space, over kernel TCP sockets.
 * Its probes fire only for the commands SPDK sampled: ntprof writes the
 * sampling period into the process (g_ntprof_sample_every) and SPDK applies
 * ntp_sample_hash() itself, so an unsampled command never traps into the
 * kernel. Every USDT program here therefore runs for sampled commands only;
 * the period in .rodata is checked once more so that both sides agree.
 *
 * Arrival times. SPDK reads a socket with readv(), which copies every skb
 * queued on it. Two kernel hooks keep, per SPDK thread, the arrival time of
 * the first skb copied by its latest tcp_recvmsg() call, and a PDU parsed
 * after that call takes it as its arrival. Exact while one call carries one
 * PDU (light load); when one call carries several PDUs, the later ones take
 * the first skb's time, which moves at most the spread of that batch from
 * network time to receive-stack time.
 */
#ifndef NTPROF_SPDK_BPF_H
#define NTPROF_SPDK_BPF_H

#include "common.bpf.h"

#include <bpf/usdt.bpf.h>

/* PDU types and flags as on the wire (NVMe/TCP specification). */
#define NTP_PDU_CMD 0x04
#define NTP_PDU_RSP 0x05
#define NTP_PDU_H2C_DATA 0x06
#define NTP_PDU_C2H_DATA 0x07
#define NTP_PDU_R2T 0x09

/* Initiator events, enum ntprof_nvme_event in SPDK's spdk_internal/ntprof.h. */
#define NTP_SPDK_EV_C2H 1
#define NTP_SPDK_EV_R2T 2
#define NTP_SPDK_EV_RSP 3
#define NTP_SPDK_EV_DONE 4

/* Copy the identifying fields out of an SPDK struct spdk_nvme_cmd. */
static __always_inline int spdk_read_cmd(u64 cmd, struct ntp_cmd_id* id,
                                         u32* bytes) {
  struct ntp_nvme_cmd c;

  if (bpf_probe_read_user(&c, sizeof(c), (const void*)cmd)) return -1;
  id->opcode = c.opcode;
  id->cid = c.command_id;
  id->nsid = c.nsid;
  id->slba = c.slba;
  id->nlb = c.length;
  /* NVMe/TCP data block SGL descriptor: the byte count is in bytes 8-11 */
  *bytes = (u32)c.dptr[1];
  return 0;
}

/* Filters shared by both sides; @write is the command's direction. */
static __always_inline bool spdk_selected(const struct ntp_cmd_id* id,
                                          u32 bytes, bool write) {
  if (filter.io_type != 2 && filter.io_type != write) return false;
  if (!ntp_sampled(id->cid, id->slba, id->nlb)) return false;
  return ntp_size_matches(bytes);
}

/* Address of a user-space object, hashed into a direct-mapped table. */
static __always_inline u32 spdk_slot(u64 addr) {
  return ntp_slot(addr, NTP_REC_SLOTS);
}

/* ---- arrival times ---- */

struct spdk_rx {
  u64 tid;
  u64 first; /* arrival of the first skb copied by the latest tcp_recvmsg() */
};
NTP_CPU_CTX(spdk_rx, struct spdk_rx);

static __always_inline bool spdk_process(void) {
  u32 tgid = bpf_get_current_pid_tgid() >> 32;

#pragma unroll
  for (int i = 0; i < NTP_MAX_PIDS; i++)
    if (i < filter.npids && filter.pids[i] == tgid) return true;
  return false;
}

static __always_inline int spdk_recvmsg(void) {
  u32 zero = 0;
  struct spdk_rx* r;

  if (!filter.npids || !spdk_process()) return 0;
  r = bpf_map_lookup_elem(&spdk_rx, &zero);
  if (!r) return 0;
  r->tid = ntp_tid();
  r->first = 0;
  return 0;
}

static __always_inline int spdk_copy(const struct sk_buff* skb) {
  struct spdk_rx* r = NTP_CTX_GET(spdk_rx);

  if (r && !r->first) r->first = ntp_skb_arrival(skb, bpf_ktime_get_ns());
  return 0;
}

/* Arrival time of the PDU this thread is parsing, or @now. */
static __always_inline u64 spdk_arrival(u64 now) {
  struct spdk_rx* r = NTP_CTX_GET(spdk_rx);

  return (r && r->first && r->first <= now) ? r->first : now;
}

SEC("fentry/tcp_recvmsg")
int BPF_PROG(ntp_s_recvmsg_fe) { return spdk_recvmsg(); }

SEC("kprobe/tcp_recvmsg")
int BPF_KPROBE(ntp_s_recvmsg_kp) { return spdk_recvmsg(); }

SEC("fentry/skb_copy_datagram_iter")
int BPF_PROG(ntp_s_copy_fe, const struct sk_buff* skb) { return spdk_copy(skb); }

SEC("kprobe/skb_copy_datagram_iter")
int BPF_KPROBE(ntp_s_copy_kp, const struct sk_buff* skb) {
  return spdk_copy(skb);
}

/* ---- records ---- */

static __always_inline void spdk_emit(void* rec, u32 size, u32 emitted) {
  if (bpf_ringbuf_output(&events, rec, size, 0))
    ntp_count(NTP_CNT_RB_DROPS, 1);
  else
    ntp_count(emitted, 1);
  ntp_inflight_add(-1);
}

#endif /* NTPROF_SPDK_BPF_H */
