/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Maps and helpers shared by every ntprof BPF object.
 *
 * Each object (host, target; TCP, RDMA) has its own copy of these maps. When
 * several objects run in one process, user space makes them share
 * skb_arrival (bpf_map__reuse_fd) and loads only one netif_receive_skb hook.
 */
#ifndef NTPROF_COMMON_BPF_H
#define NTPROF_COMMON_BPF_H

#include "kernel_types.h"

#include <linux/bpf.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "ntprof.h"

char LICENSE[] SEC("license") = "GPL";

/* Set by user space before load; read-only afterwards. */
const volatile struct ntp_filter filter = {
    .sample_every = 1,
    .io_type = 2,
};

/*
 * Set by user space once every program is attached. Commands that started
 * earlier would miss events (their hooks were not attached yet), so neither
 * records nor queue counters include them.
 */
volatile bool ntp_armed;

/* Completed records for user space. Size is set by user space. */
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 24);
} events SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, NTP_CNT_NR);
  __type(key, u32);
  __type(value, u64);
} counters SEC(".maps");

/* skb -> time it entered the network stack (netif_receive_skb). */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 1 << 16);
  __type(key, u64);
  __type(value, u64);
} skb_arrival SEC(".maps");

static __always_inline void ntp_count(u32 counter, u64 delta) {
  u64* v = bpf_map_lookup_elem(&counters, &counter);

  if (v) *v += delta;
}

static __always_inline u64 ntp_tid(void) { return bpf_get_current_pid_tgid(); }

/*
 * An skb address is reused once the skb is freed, and the map keeps the old
 * stamp until it is overwritten or evicted. A stamp older than this is taken
 * to belong to an earlier skb at the same address.
 */
#define NTP_SKB_STAMP_MAX_AGE_NS 1000000000ULL

/* Arrival time of an skb, or @now when it was not stamped. */
static __always_inline u64 ntp_skb_arrival(const struct sk_buff* skb, u64 now) {
  u64 key = (u64)skb;
  u64* t = bpf_map_lookup_elem(&skb_arrival, &key);

  if (!t || *t > now || now - *t > NTP_SKB_STAMP_MAX_AGE_NS) return now;
  return *t;
}

static __always_inline bool ntp_sampled(u16 cid, u64 slba, u16 nlb) {
  if (filter.sample_every <= 1) return true;
  return ntp_sample_hash(cid, slba, nlb) % filter.sample_every == 0;
}

/* Copy the identifying fields out of a wire-format NVMe command. */
static __always_inline int ntp_read_cmd(const void* cmd, struct ntp_cmd_id* id) {
  struct ntp_nvme_cmd c;

  if (bpf_probe_read_kernel(&c, sizeof(c), cmd)) return -1;
  id->opcode = c.opcode;
  id->cid = c.command_id;
  id->nsid = c.nsid;
  id->slba = c.slba;
  id->nlb = c.length;
  return 0;
}

static __always_inline bool ntp_is_rw(u8 opcode) {
  return opcode == NTP_NVME_CMD_READ || opcode == NTP_NVME_CMD_WRITE;
}

/* Set a timestamp only the first time it is seen. */
static __always_inline void ntp_stamp_once(u64* slot, u64 now) {
  if (!*slot) *slot = now;
}

/*
 * Arrival timestamps for skbs. The packet is stamped after GRO, where
 * v1 read skb->tstamp; when TCP later coalesces it into an earlier skb of the
 * receive queue, the earlier skb's time is used.
 */
SEC("raw_tp/netif_receive_skb")
int BPF_PROG(ntp_netif_rx, struct sk_buff* skb) {
  u64 key = (u64)skb;
  u64 now = bpf_ktime_get_ns();

  if (filter.ifindex && BPF_CORE_READ(skb, dev, ifindex) != (int)filter.ifindex)
    return 0;
  bpf_map_update_elem(&skb_arrival, &key, &now, BPF_ANY);
  return 0;
}

#endif /* NTPROF_COMMON_BPF_H */
