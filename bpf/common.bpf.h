/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Maps and helpers shared by every ntprof BPF object.
 *
 * Each object (host, target; TCP, RDMA; SPDK) has its own copy of these maps.
 * When several objects run in one process, user space makes them share
 * `events` (bpf_map__reuse_fd), so records come out in emission order.
 *
 * Function hooks come in two flavours with the same body: fentry/fexit
 * (programs named *_fe, *_fx), used when the kernel has BTF, and
 * kprobe/kretprobe (*_kp, *_kr) for kernels without it. The loader keeps one
 * flavour. fentry/fexit cost a fraction of a kprobe, and fexit sees the
 * arguments and the return value, so no entry state has to be kept.
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

/*
 * Records of this object not yet emitted. Hooks that only add timestamps to
 * sampled records return at once while it is 0, which is most of the time
 * when one command in N is sampled. A record lost to a slot collision keeps
 * it above 0 until the slot is reused (only the shortcut is lost).
 */
volatile __s64 ntp_inflight; /* __s64: the skeleton exposes it to user space */

static __always_inline void ntp_inflight_add(s64 d) {
  __sync_fetch_and_add(&ntp_inflight, d);
}

/* Is there anything for a timestamp-only hook to do? */
static __always_inline bool ntp_idle(void) {
  return !ntp_inflight && !filter.track_queues;
}

/* Completed records for user space. Size is set by user space. */
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 24);
} events SEC(".maps");

/*
 * Waking the reader for every record costs the emitting CPU an interrupt and
 * a wake-up, inside the I/O path. User space polls every 100 ms anyway, so
 * the reader is woken only once this much is waiting.
 */
#define NTP_RB_WAKEUP_BYTES (64 << 10)

static __always_inline u64 ntp_rb_flags(void) {
  return bpf_ringbuf_query(&events, BPF_RB_AVAIL_DATA) >= NTP_RB_WAKEUP_BYTES
             ? BPF_RB_FORCE_WAKEUP
             : BPF_RB_NO_WAKEUP;
}

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, struct ntp_counters);
} counters SEC(".maps");

/* This CPU's counters; a hook that bumps several looks them up once. */
static __always_inline struct ntp_counters* ntp_counters(void) {
  u32 zero = 0;

  return bpf_map_lookup_elem(&counters, &zero);
}

static __always_inline void ntp_count(u32 counter, u64 delta) {
  struct ntp_counters* c = ntp_counters();

  if (c && counter < NTP_CNT_NR) c->c[counter] += delta;
}

static __always_inline u64 ntp_tid(void) { return bpf_get_current_pid_tgid(); }

/*
 * ---- per-thread context in a per-CPU slot ----
 *
 * A context set at a function's entry and used by hooks inside that
 * function, on the same thread, lives in a per-CPU slot tagged with the
 * thread id: no lock and no hash. Should the thread migrate in between, the
 * tag no longer matches and the event is lost rather than misattributed.
 */
#define NTP_CPU_CTX(name, ctx_type)            \
  struct {                                     \
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);   \
    __uint(max_entries, 1);                    \
    __type(key, u32);                          \
    __type(value, ctx_type);                   \
  } name SEC(".maps")

/* The context of this thread, or NULL. @ctx_type must start with `u64 tid`. */
#define NTP_CTX_GET(map)                                         \
  ({                                                             \
    u32 __z = 0;                                                 \
    typeof(((typeof(map)*)0)->value) __c = NULL;                 \
    __c = bpf_map_lookup_elem(&map, &__z);                       \
    (__c && __c->tid == ntp_tid()) ? __c : NULL;                 \
  })

/*
 * fentry/fexit arguments are typed pointers, and the verifier refuses any
 * arithmetic on them but adding offsets. A round trip through map memory
 * turns the address into a plain number that can be hashed.
 */
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, u64);
} ntp_scalar_slot SEC(".maps");

static __always_inline u64 ntp_scalar(u64 v) {
  u32 zero = 0;
  u64* slot = bpf_map_lookup_elem(&ntp_scalar_slot, &zero);

  if (!slot) return 0;
  *slot = v;
  return *(volatile u64*)slot;
}

/* Slot of a kernel address in a direct-mapped table of @nslots (power of 2). */
static __always_inline u32 ntp_slot(u64 addr, u32 nslots) {
  /* slab objects: the low bits carry little information */
  return (u32)(((addr >> 6) ^ (addr >> 26)) * 0x9e3779b1u) & (nslots - 1);
}

/*
 * ---- records in direct-mapped tables ----
 *
 * Records of sampled commands live in arrays indexed by a hash of the
 * command's kernel address (struct request, struct nvmet_req). Every hook
 * runs for every command, sampled or not; an array lookup and one compare
 * tell it in a few ns that the command has no record, where a hash map
 * lookup would take a lock-free search. Two sampled commands in flight on
 * the same slot are rare (a few hundred in flight against 16384 slots); the
 * later one takes the slot and the earlier one's record is lost (counted as
 * NTP_CNT_STALE).
 */
#define NTP_REC_SLOTS (1 << 14)

/*
 * ---- skb arrival times ----
 *
 * ntprof reads the kernel's receive timestamp of each skb: while
 * ntprof runs, user space holds a socket with SO_TIMESTAMPNS, and the stack
 * then stamps every received packet with CLOCK_REALTIME in skb->tstamp
 * (net_timestamp_check(), after GRO). No hook runs per packet. When TCP
 * coalesces a packet into an earlier skb, the earlier skb's time is used.
 */
#define NTP_SKB_STAMP_MAX_AGE_NS 1000000000ULL

static __always_inline bool ntp_plausible(u64 ts, u64 now) {
  return ts && ts <= now && now - ts < NTP_SKB_STAMP_MAX_AGE_NS;
}

/* Arrival time of an skb, or @now when it carries no usable timestamp. */
static __always_inline u64 ntp_skb_arrival(const struct sk_buff* skb, u64 now) {
  s64 t = BPF_CORE_READ(skb, tstamp);
  u64 mono;

  if (t <= 0) return now;
  mono = (u64)(t - filter.real_minus_mono);
  if (ntp_plausible(mono, now)) return mono;
  /* a stack that kept a monotonic delivery time in it */
  if (ntp_plausible((u64)t, now)) return (u64)t;
  return now;
}

/* ---- NVMe ---- */

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

static __always_inline bool ntp_size_matches(u32 bytes) {
  if (filter.n_sizes) {
#pragma unroll
    for (int i = 0; i < NTP_MAX_SIZE_FILTERS; i++)
      if (i < filter.n_sizes && filter.sizes[i] == bytes) return true;
    return false;
  }
  if (filter.min_bytes && bytes < filter.min_bytes) return false;
  if (filter.max_bytes && bytes > filter.max_bytes) return false;
  return true;
}

/* Set a timestamp only the first time it is seen. */
static __always_inline void ntp_stamp_once(u64* slot, u64 now) {
  if (!*slot) *slot = now;
}

static __always_inline u16 ntp_clamp_u16(s32 v) {
  return v < 0 ? 0 : v > 0xffff ? 0xffff : (u16)v;
}

#endif /* NTPROF_COMMON_BPF_H */
