/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPDK's NVMe/TCP transports, through the hooks spdk/ntprof-usdt-*.patch
 * adds to SPDK.
 *
 * SPDK samples commands itself (ntprof writes the sampling period into the
 * process; SPDK applies ntp_sample_hash()) and, for a sampled command,
 * timestamps every stage boundary in the process with CLOCK_MONOTONIC, the
 * clock of bpf_ktime_get_ns(). When the command is done on a side, one USDT
 * probe hands over its struct spdk_times, and the programs here turn it into
 * the same record the kernel objects emit. A probe traps into the kernel
 * (about a microsecond); firing one per command and side, after its last
 * event, keeps that cost out of the times.
 *
 * Two times only the kernel sees are kept per SPDK thread, in rings, and
 * matched with the command's times when it is done:
 *  - packet arrival: for each tcp_recvmsg() that copied data, its start and
 *    the receive timestamp of its first skb. A PDU parsed at time t
 *    arrived with the first skb of the latest such call started before t.
 *    Exact while a read carries one PDU (light load); when one read carries
 *    several, the later ones take the first skb's time, which moves at most
 *    the spread of that read from network time to receive-stack time.
 *  - the target's sends: the entries of tcp_sendmsg(). A PDU that SPDK's
 *    socket layer saw taken at time t was handed to TCP at the latest entry
 *    before t.
 */
#ifndef NTPROF_SPDK_BPF_H
#define NTPROF_SPDK_BPF_H

#include "common.bpf.h"

#include <bpf/usdt.bpf.h>

/*
 * The start of struct ntprof_times of SPDK's include/spdk_internal/ntprof.h,
 * and its event indices. Change only together with the SPDK patch.
 */
#define SPDK_TIMES 10

struct spdk_times {
  u64 t[SPDK_TIMES]; /* ns, CLOCK_MONOTONIC; 0 = did not happen */
  u16 r2ts;
  u16 flags;
  u8 on;
  u8 reserved[3];
};

#define SPDK_F_C2H_SUCCESS (1 << 0)

enum spdk_host_event {
  SPDK_H_SUBMIT = 0,
  SPDK_H_CMD_SENT,
  SPDK_H_R2T,
  SPDK_H_H2C_START,
  SPDK_H_H2C_SENT,
  SPDK_H_C2H,
  SPDK_H_RSP,
  SPDK_H_DONE,
};

enum spdk_tgt_event {
  SPDK_T_CMD = 0,
  SPDK_T_R2T_QUEUED,
  SPDK_T_R2T_SENT,
  SPDK_T_H2C,
  SPDK_T_EXEC,
  SPDK_T_EXECUTED,
  SPDK_T_RSP_QUEUED,
  SPDK_T_C2H_SENT,
  SPDK_T_RSP_SENT,
};

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

static __always_inline bool spdk_process(void) {
  u32 tgid = bpf_get_current_pid_tgid() >> 32;

#pragma unroll
  for (int i = 0; i < NTP_MAX_PIDS; i++)
    if (i < filter.npids && filter.pids[i] == tgid) return true;
  return false;
}

/* ---- per-thread rings of (time, value), in a per-CPU slot ---- */

#define SPDK_RING 512 /* a power of 2 */

struct spdk_ring {
  u64 tid;
  u64 n;            /* entries appended since the thread took the slot */
  u64 t[SPDK_RING]; /* ascending */
  u64 v[SPDK_RING];
};

#define SPDK_RING_MAP(name)                  \
  struct {                                   \
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY); \
    __uint(max_entries, 1);                  \
    __type(key, u32);                        \
    __type(value, struct spdk_ring);         \
  } name SEC(".maps")

/* This thread's ring; a thread that takes the CPU's slot starts it afresh. */
static __always_inline struct spdk_ring* spdk_ring_own(void* map) {
  u32 zero = 0;
  struct spdk_ring* r = bpf_map_lookup_elem(map, &zero);
  u64 tid = ntp_tid();

  if (r && r->tid != tid) {
    r->tid = tid;
    r->n = 0;
  }
  return r;
}

static __always_inline void spdk_ring_add(struct spdk_ring* r, u64 t, u64 v) {
  u32 i = r->n & (SPDK_RING - 1);

  r->t[i] = t;
  r->v[i] = v;
  r->n++;
}

SPDK_RING_MAP(spdk_recv_ring); /* (start of a reading call, its first skb's arrival) */
SPDK_RING_MAP(spdk_send_ring); /* (entry of a sending call, the same): target */

static __always_inline struct spdk_ring* spdk_ring_get(int send) {
  u32 zero = 0;

  return bpf_map_lookup_elem(send ? (void*)&spdk_send_ring : (void*)&spdk_recv_ring,
                             &zero);
}

/* A binary search, one halving per bpf_loop() step. */
struct spdk_find {
  u64 x;
  u64 base; /* logical index i is slot (base + i) % SPDK_RING */
  u64 lo, hi;
  int send;
};

static long spdk_find_step(u32 i, void* ctx) {
  struct spdk_find* f = ctx;
  struct spdk_ring* r;
  u64 mid;

  if (f->lo >= f->hi) return 1;
  r = spdk_ring_get(f->send);
  if (!r) return 1;
  mid = (f->lo + f->hi) / 2;
  if (r->t[(f->base + mid) & (SPDK_RING - 1)] <= f->x)
    f->lo = mid + 1;
  else
    f->hi = mid;
  return 0;
}

/* Value of this thread's latest entry at or before @x, or 0. */
static __always_inline u64 spdk_ring_find(int send, u64 x) {
  struct spdk_ring* r = spdk_ring_get(send);
  struct spdk_find f = {};
  u64 n;

  if (!r || r->tid != ntp_tid() || !x) return 0;
  n = r->n < SPDK_RING ? r->n : SPDK_RING;
  f.x = x;
  f.base = r->n - n;
  f.hi = n;
  f.send = send;
  bpf_loop(10, spdk_find_step, &f, 0); /* log2(SPDK_RING) + 1 */
  return f.lo ? r->v[(f.base + f.lo - 1) & (SPDK_RING - 1)] : 0;
}

/* ---- packet arrival ---- */

struct spdk_rx {
  u64 tid;
  u64 start;  /* entry of the thread's current tcp_recvmsg() */
  u64 copied; /* that call copied an skb already */
};
NTP_CPU_CTX(spdk_rx, struct spdk_rx);

static __always_inline int spdk_recvmsg(void) {
  u32 zero = 0;
  struct spdk_rx* c;

  if (!filter.npids || !spdk_process()) return 0;
  c = bpf_map_lookup_elem(&spdk_rx, &zero);
  if (!c) return 0;
  c->tid = ntp_tid();
  c->start = bpf_ktime_get_ns();
  c->copied = 0;
  return 0;
}

static __always_inline int spdk_copy(const struct sk_buff* skb) {
  struct spdk_rx* c = NTP_CTX_GET(spdk_rx);
  struct spdk_ring* r;

  if (!c || c->copied) return 0;
  c->copied = 1;
  r = spdk_ring_own(&spdk_recv_ring);
  if (r) spdk_ring_add(r, c->start, ntp_skb_arrival(skb, bpf_ktime_get_ns()));
  return 0;
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

/*
 * Arrival of a PDU this thread parsed at @parsed (0: not parsed), not before
 * @floor; @parsed itself when unknown.
 */
static __always_inline u64 spdk_arrival(u64 parsed, u64 floor) {
  u64 a = spdk_ring_find(0, parsed);

  return (a && a >= floor && a <= parsed) ? a : parsed;
}

/* ---- records ---- */

static __always_inline void spdk_submit(void* rec, u32 emitted) {
  bpf_ringbuf_submit(rec, ntp_rb_flags());
  ntp_count(emitted, 1);
}

#endif /* NTPROF_SPDK_BPF_H */
