# ntprof v2 design

ntprof v2 measures the same thing as the NSDI'25 ntprof (v1, `src/` and
`kernel_patches/` in this repository): the latency of each NVMe-over-Fabrics
I/O, split into the stages of the path from the initiator's block layer to the
target's device and back (S1-S9 in the paper's Figure 4). v1 did it with 23
tracepoints patched into `nvme-tcp` / `nvmet-tcp`, a patched `blk-core.c` and
two kernel modules. v2 does it with eBPF on unmodified kernels, for NVMe/TCP
and NVMe/RDMA, and on SPDK's user-space NVMe/TCP through USDT probes
(section 8).

## 1. What changed from v1, and why

| | v1 | v2 |
|---|---|---|
| Instrumentation | tracepoints added to `drivers/nvme/{host,target}/tcp.c` | stock tracepoints, raw tracepoints, fentry/fexit or kprobes |
| Kernel | patched 5.15, rebuilt and rebooted | any kernel with BPF; tested on 6.8 (Ubuntu) and 6.11 |
| `block_rq_complete` | needed `EXPORT_TRACEPOINT_SYMBOL` in `blk-core.c` | raw tracepoints need no export |
| Target -> host timestamps | `struct ntprof_stat` appended to every PDU (wire format changed) | joined in user space; the wire is untouched |
| Sampling decision host -> target | `stat.tag` bit in the command PDU | both sides hash (cid, slba, nlb) |
| Analysis | in the kernel module (`analyzer.c`) | in user space, online or from record files |
| Output | text in the terminal | text, JSON, raw records (CSV via `dump`), LAD between runs |
| Transports | NVMe/TCP | NVMe/TCP, NVMe/RDMA; SPDK NVMe/TCP (initiator and target) |

## 2. Hooks

A hook is used only where the kernel already has an attach point: a
tracepoint, or a function the compiler kept out of line. The functions v1
instrumented are mostly `static` and often inlined (on 6.11 `nvme_tcp_queue_request`,
`nvme_tcp_done_send_req`, `nvme_tcp_handle_c2h_data`, `nvme_tcp_handle_r2t`,
`nvme_tcp_process_nvme_cqe` and all of `nvmet-tcp`'s send functions are), so v2
watches the functions around them, or exported kernel functions they call,
and tells the NVMe/TCP calls apart with a per-thread context.

### Initiator (`bpf/nvme_tcp_host.bpf.c`)

| v1 event (tracepoint position) | v2 hook | v2 event |
|---|---|---|
| BLK_SUBMIT (`rq->start_time_ns`) | same field, read at `nvme_setup_cmd` | `NTP_H_START` |
| NVME_TCP_QUEUE_RQ (after `blk_mq_start_request`) | `raw_tp/nvme_setup_cmd` (cid, opcode, LBA), inside `nvme_tcp_queue_rq`; the entry of `nvme_tcp_queue_rq` itself only with `--exact-queues` | `NTP_H_QUEUE_RQ` |
| NVME_TCP_DONE_SEND_REQ (`nvme_tcp_done_send_req`, inlined) | exit of `nvme_tcp_try_send_{cmd_pdu,data,ddgst}` returning 1 with `queue->request` cleared | `NTP_H_CMD_SENT`, `NTP_H_DATA_SENT` |
| (none) | entry of `nvme_tcp_try_send_data_pdu` after an R2T | `NTP_H_DATA_START` |
| NVME_TCP_RECV_SKB (`skb->tstamp`, socket timestamps enabled by the patch) | `skb->tstamp` read at `nvme_tcp_recv_skb`; ntprof turns receive timestamps on (section 7) | `NTP_H_*_ARR` |
| NVME_TCP_HANDLE_C2H_DATA (inlined) | first `skb_copy_datagram_iter` while `queue->pdu` holds a C2H header | `NTP_H_C2H_PROC` |
| NVME_TCP_HANDLE_R2T (inlined on 6.11) | `nvme_tcp_setup_h2c_data_pdu` while receiving | `NTP_H_R2T_PROC` |
| NVME_TCP_PROCESS_NVME_CQE (inlined) | `blk_mq_complete_request_remote` while receiving | `NTP_H_RSP_PROC` |
| (none) | `raw_tp/nvme_complete_rq` | `NTP_H_NVME_DONE` |
| BLK_RQ_COMPLETE | `raw_tp/block_rq_complete` | `NTP_H_BLK_DONE` |

### Target (`bpf/nvme_tcp_target.bpf.c`)

| v1 event | v2 hook | v2 event |
|---|---|---|
| NVMET_TCP_TRY_RECV_PDU (`SO_TIMESTAMP` of `kernel_recvmsg`) | `skb->tstamp` of the last skb this thread copied (`skb_copy_datagram_iter`) from the command's queue socket | `NTP_T_CMD_ARR` |
| NVMET_TCP_DONE_RECV_PDU (inlined) | `raw_tp/nvmet_req_init` | `NTP_T_CMD_PROC` |
| (R2T queued) | `nvmet_tcp_queue_response` while data is still due | `NTP_T_R2T_QUEUED` |
| NVMET_TCP_TRY_SEND_R2T (inlined) | `sock_sendmsg` on a queue socket of a sampled command, `queue->snd_cmd->state == SEND_R2T`, offset 0 | `NTP_T_R2T_SENT` |
| NVMET_TCP_HANDLE_H2C_DATA_PDU (inlined) | `nvmet_tcp_build_pdu_iovec` after an R2T | `NTP_T_DATA_*` |
| NVMET_TCP_EXEC_{READ,WRITE}_REQ | `nvmet_{bdev,file}_execute_rw`, `nvmet_passthru_execute_cmd` | `NTP_T_EXEC` |
| (none) | `raw_tp/block_rq_issue` / `block_rq_complete` of the backing request (`bio->bi_private` is the `nvmet_req`) | `NTP_T_BE_ISSUE`, `NTP_T_BE_DONE` |
| (none) | `raw_tp/nvmet_req_complete` | `NTP_T_COMPLETE` |
| NVMET_TCP_QUEUE_RESPONSE | `nvmet_tcp_queue_response` for the completion | `NTP_T_RSP_QUEUED` |
| (none) | `sock_sendmsg`, state `SEND_DATA_PDU` | `NTP_T_C2H_SENT` |
| NVMET_TCP_TRY_SEND_RESPONSE (inlined) | `sock_sendmsg`, state `SEND_RESPONSE` | `NTP_T_RSP_SENT` |

Telling NVMe/TCP apart without a hook on its entry points: `nvme_setup_cmd`
and `nvmet_req_init` run for every NVMe transport. The initiator follows a
request's driver PDU to its queue and the target follows the `nvmet_sq` to
the queue that embeds it. Either way, the command is NVMe/TCP's when the
queue's socket has `sk_user_data` pointing back to that queue. Both drivers
set this when they take over a socket. For another transport, the same
offsets hold unrelated data and the test fails. The target's `sock_sendmsg`
hook uses the same link (socket to queue) and runs only while a sampled
command is in flight.

### NVMe/RDMA (`bpf/nvme_rdma_host.bpf.c`, `bpf/nvme_rdma_target.bpf.c`)

v1 had no RDMA support; the events keep the TCP names where the meaning
carries over ("sent" = posted to the queue pair). The block-layer and
nvme-core hooks are the same as for TCP (`bpf/host.bpf.h`,
`bpf/target.bpf.h`).

| Side | v2 hook | v2 event |
|---|---|---|
| initiator | entry of `nvme_rdma_queue_rq` | `NTP_H_QUEUE_RQ` |
| initiator | `nvme_rdma_post_send` returning 0 | `NTP_H_CMD_SENT` |
| initiator | `nvme_rdma_send_done` | `NTP_H_SEND_DONE` |
| initiator | `nvme_rdma_recv_done` (the response's CQE; cid from the CQE) | `NTP_H_RSP_ARR` |
| initiator | `blk_mq_complete_request_remote` in a completion handler | `NTP_H_RSP_PROC` |
| target | `nvmet_rdma_recv_done` (the command's CQE) | `NTP_T_CMD_ARR` |
| target | `nvmet_rdma_execute_command` for a write whose data is not inline | `NTP_T_R2T_SENT` (RDMA READ posted), flag `RDMA_READ` |
| target | `nvmet_rdma_read_data_done` | `NTP_T_DATA_ARR`, `NTP_T_DATA_PROC` |
| target | entry / exit of `nvmet_rdma_queue_response` | `NTP_T_RSP_QUEUED` / `NTP_T_C2H_SENT` (reads), `NTP_T_RSP_SENT` |
| target | `nvmet_rdma_send_done` (the response's send completion; for a read, after its data's RDMA WRITE) | `NTP_T_SEND_DONE` |

The target record is emitted at the later of `nvmet_rdma_queue_response`'s
return and the send completion: the completion can be handled on another
CPU first. Each side sets its bit with an atomic fetch-or, and the one that
completes the pair emits (BPF ISA v3, Linux 5.12, for this object only).

### Hook flavours

Every function hook exists twice with the same body: as fentry/fexit
(`*_fe`, `*_fx`) and as kprobe/kretprobe (`*_kp`, `*_kr`). fentry/fexit cost
less per call (section 7), and fexit sees the return value without
remembering the arguments. They need the kernel's BTF, and they can only be
attached when no CPU stays busy (below). The loader keeps fentry/fexit when
both hold, and kprobes otherwise or with `--kprobes`.

**Busy CPUs.** Attaching an fentry/fexit program goes through
`register_ftrace_direct()`, which calls `synchronize_rcu_tasks()`;
detaching waits the same way. That grace period ends only when every
runnable task has switched out voluntarily. A thread that stays runnable,
such as a polling thread or a fio job whose completions are always ready,
holds it up. The attaching thread meanwhile sleeps uninterruptibly. The
kernel stack of a waiting thread is `link_create -> bpf_trampoline_update
-> register_ftrace_direct -> synchronize_rcu_tasks`.

On the development guest (Linux 6.8, `PREEMPT_DYNAMIC`), the load was four
fio jobs (4 KB random reads, iodepth 8, CPUs 4-7, loopback NVMe/TCP). With
one program attached after the other, attaching both sides took 33 s and
60 s in two runs, and stopping took 27 s. kprobes attached in 0.5 s and
detached in 1 s. SPDK's reactors did not hold the grace period up in our
runs. ntprof handles this in three places:

- Attaching from 16 threads. ftrace drops its lock before it waits, so
  concurrent waits overlap. Both sides' fentry programs now attach in
  1.7 s on an idle guest (5 s one after the other) and in 8.5 s under the
  load above. Detaching is parallel too, but takes 1.8 s idle all the same.
- Before choosing fentry, a disposable grandchild process attaches a
  one-instruction fentry program to `bpf_fentry_test1`, which runs only in
  BPF selftests. If that does not succeed within 2 s, ntprof warns and uses
  kprobes. Nobody waits for the grandchild. One grace period can be quick
  even under load, so the check sometimes passes and the attach is then
  slow.
- `record` runs in a child process, and the parent holds no BPF object.
  If the child has not attached within 10 s, the parent kills it and starts
  over with kprobes. The killed attempt dies once its grace period ends;
  its programs were never armed, and the record file is opened only after
  attaching. Under the load above, three starts took 2.5 s, 2.5 s and
  3.7 s, and all recorded. At the end, the child prints the report, tells
  the parent, and only then detaches. The parent returns once the hooks are
  gone or after 3 s, whichever comes first, and in the second case says
  that removal continues in the background. With `--fentry` there is no
  restart; a second Ctrl-C leaves a stuck attach behind.

A program whose attach point is missing on the running kernel is not loaded;
`ntprof probe` lists them. The records then lack that event, and the model
falls back where it can (section 4). For measurements,
`NTPROF_SKIP=prog,prog` leaves the named programs out.

All objects of one run share a single ring buffer, so records come out in
emission order. A program wakes the reader only when 64 KB are waiting;
the reader polls every 100 ms anyway. Throughput counters are one per-CPU
struct, so each hook does one map lookup for them.

## 3. Records and their join

Each side keeps a record per sampled command in a direct-mapped BPF array
(indexed by a hash of the command's address) and emits it on a ring buffer
when the command leaves that side: the target at its response, the host at
`block_rq_complete` (`bpf/ntprof.h`). Records carry
`CLOCK_MONOTONIC` timestamps; only differences of timestamps from the same
machine are ever taken, so host and target clocks need no synchronisation.

**Join.** User space matches a host record with the target record of the same
(cntlid, qid, cid) and checks opcode, nsid, slba and nlb (`src/join.c`). The
cid alone repeats: nvme folds only a 4-bit generation counter into it, so at
queue depth 1 a (queue, cid) pair recurs every 16 commands.

**Sampling.** A command is sampled when `hash(cid, slba, nlb) % N == 0`. Both
sides compute the same hash, so they sample the same commands without the tag
v1 carried in the PDU.

**Order.** Within one process (`--role both`) the host and target programs
share one ring buffer, so a command's target record (emitted when its response
is sent) is normally consumed before its host record. With SPDK the host can
complete first (section 8); a host record without its target record then
waits up to 100 ms in the joiner before it counts as unmatched. Across
machines, the two record files are joined offline (`ntprof report host.ntp
target.ntp`), which is order-independent.

**Arming.** Programs attach one by one. Nothing is recorded or counted until
every program is attached; a command that started earlier would miss events.

## 4. The stage model (`src/model.c`)

Notation: H = host timestamp, T = target timestamp, as in `bpf/ntprof.h`.
For each round trip between the host and the target, the host waits from
handing its PDU to TCP until the target's answer reaches its network stack;
the target is busy from the arrival of the host's PDU until its answer is
handed to TCP. The **network** share of the round trip is the host's wait minus
the target's residence: wire, NICs and the TCP/IP work neither side's
transport sees (v1, `src/host/breakdown.c`). Over loopback it is about 0 and
can be slightly negative (the target may see a packet before the host's
`sendmsg` returns).

| Component | Stage | Read | Write, data in the capsule | Write, data after R2T |
|---|---|---|---|---|
| blk-mq submission | S1(S) | H_QUEUE_RQ - H_START | same | same |
| initiator transport submission | S2(S) | H_CMD_SENT - H_QUEUE_RQ | same | + (H_DATA_START - H_R2T_PROC) |
| network | S3-S5 | (H_C2H_ARR - H_CMD_SENT) - (T_C2H_SENT - T_CMD_ARR) | (H_RSP_ARR - H_CMD_SENT) - (T_RSP_SENT - T_CMD_ARR) | two round trips: (H_R2T_ARR - H_CMD_SENT) - (T_R2T_SENT - T_CMD_ARR) + (H_RSP_ARR - H_DATA_START) - (T_RSP_SENT - T_DATA_ARR) |
| target receive stack | S5(S) | T_CMD_PROC - T_CMD_ARR | same | + (T_DATA_PROC - T_DATA_ARR) |
| target transport submission | S6(S) | T_EXEC - T_CMD_PROC | same (includes receiving the data) | (T_R2T_SENT - T_CMD_PROC) + (T_EXEC - T_DATA_PROC) |
| target block layer and device | S7-S9 | T_COMPLETE - T_EXEC | same | same |
| target transport completion | S6(C) | T_C2H_SENT - T_COMPLETE | T_RSP_SENT - T_COMPLETE | same |
| initiator receive stack | S3(C) | H_C2H_PROC - H_C2H_ARR | H_RSP_PROC - H_RSP_ARR | + (H_R2T_PROC - H_R2T_ARR) |
| initiator transport completion | S2(C) | H_RSP_PROC - H_C2H_PROC | 0 | 0 |
| blk-mq completion | S1(C) | H_BLK_DONE - H_RSP_PROC | same | same |

The components add up to the end-to-end latency H_BLK_DONE - H_START exactly
(tested in `tests/test_model.c`). Details split a component further: the
target's block layer and device (S7(S), S8-S9, S7(C), bdev backend only), and
the completion IPI (`nvme_complete_rq` - CQE parsed). The stage totals S1,
S2, S3-S5, S6, S7-S9 are what LAD is computed on.

For a write after R2T the second round trip starts when the host *starts*
sending the data: the target receives the data while the host is still
sending it, so the transfer belongs to both the host's wait and the target's
residence and cancels out of the network share.

Without a target record (host only, or a target record that misses events)
the network and target components are reported together as "network and
target (no target record)", S3-S9.

**NVMe/RDMA.** The host CPU sees one round trip: the command posted, the
response's CQE polled; the target moves the data by RDMA WRITE (reads) or
RDMA READ (writes) without the host CPU. So the host's wait is H_RSP_ARR -
H_CMD_SENT, its receive stack H_RSP_PROC - H_RSP_ARR, and S2(C) is 0. On the
target, a write's RDMA READ (T_DATA_PROC - T_R2T_SENT) is wire and remote-NIC
time seen from the target: it counts as network, not as target residence.
S6(S) is then (T_R2T_SENT - T_CMD_PROC) + (T_EXEC - T_DATA_PROC).

The detail component "data transfer (RDMA, at the target)", under S3-S5, is
the time the data spends crossing the network as the target sees it: a
write's RDMA READ (T_DATA_PROC - T_R2T_SENT), and a read's RDMA WRITE with
its response, from posting to the send completion (T_SEND_DONE -
T_C2H_SENT). These are the spans a staged pipeline times as its NIC stage:
an RDMA READ into host memory before a drive write, or an RDMA WRITE after a
drive read. With S7-S9, the drive stage, one NVMe/RDMA run then shows both
stages of each request. A read's span includes the initiator NIC's
acknowledgement on its way back, which the initiator's wait does not, so it
can exceed the read's share of S3-S5. A write whose data fits in the command
(inline, 4 KiB by default) has no such span.

## 5. Queue occupancy

v1 had the `ENABLE_QUEUE_LENGTH` switch but did not compute queue lengths.
v2 reports, per component, the mean number of commands in it by Little's law:
L = (command rate) x (mean time in the component), the rate being the group's
records times the sampling period over the time they span. This costs
nothing at run time and is exact for means in steady state.

`--exact-queues` (ini `QUEUE_TRACKING=exact`) additionally counts, per
NVMe/TCP queue, the commands not yet sent and not yet completed (host) and
not yet answered (target), as seen by each arriving command. It hooks
`nvme_tcp_queue_rq` and needs map operations on every I/O, sampled or not.

## 6. CO-RE and BTF

Kernel structures are read through minimal local declarations
(`bpf/kernel_types.h`) marked `preserve_access_index`: libbpf resolves each
member offset against the running kernel's BTF at load time, so no
`vmlinux.h` is needed to build. NVMe commands and PDU headers are read with
their wire-format layouts under names of their own (`ntp_*`), so that no
relocation is emitted for them.

Kernels built without `CONFIG_DEBUG_INFO_BTF` but with DWARF in their modules
work too: `tools/gen-btf.sh` turns the DWARF of `nvme-tcp.ko` / `nvmet-tcp.ko`
into BTF with pahole, and `--btf DIR` makes libbpf relocate against it. Such
kernels run the kprobe flavour (fentry needs real kernel BTF).

## 7. Accuracy and its limits

- **Packet arrival.** As in v1, the arrival of a PDU is the receive
  timestamp of its skb (`skb->tstamp`). The kernel stamps every received
  packet (`net_timestamp_check()`, after GRO) while any socket asks for
  receive timestamps. v1's patch enabled them; v2 holds a UDP socket with
  `SO_TIMESTAMPNS` open while it runs. No hook runs per packet. The stamp is
  `CLOCK_REALTIME`; ntprof converts it with the realtime-to-monotonic offset
  measured at start, so a clock step during a run (NTP, `date -s`) shifts
  arrivals until the next run. A stamp that is missing or not within the
  last second falls back to the time the packet was read. When TCP coalesces
  a later skb into an earlier one in the receive queue, the earlier skb's
  stamp is used, so the arrival appears earlier.
- **Context.** Per-thread context lives in per-CPU slots tagged with the
  thread id; if a thread migrates between a function's entry and the hook
  that uses the context, the event is lost, not misattributed.
- **Not covered:** NVMe/TCP with TLS (`nvme connect --tls`), polled-mode
  completions on the target, admin commands, and commands other than read and
  write. Data digests are covered on the host (`skb_copy_and_hash_datagram_iter`).
- **Overhead.** The kernel hooks run for every I/O. Sampling only decides
  which commands get a record. The hooks that only add timestamps return
  after one array lookup while no sampled command is in flight. What is left
  at 1 in 1000 is mostly the fixed cost of a BPF program run at each hook
  that every command passes: the sampling decision (`nvme_setup_cmd`,
  `nvmet_req_init`), the target's copy hook, which notes every skb it
  copies, and the initiator's receive hooks, which return at once. Measured in a
  16-vCPU guest with initiator and target together over loopback (Linux 6.8,
  fentry, null_blk, 2 I/O queues, 2026-10-14): fio, 4 KB random reads at
  queue depth 1 on one CPU, mean of 3 runs of 8 s:

  | profiled | mean latency | added |
  |---|---|---|
  | nothing | 29.4 us | |
  | initiator, 1 in 1000 | 30.3 us | +0.9 us |
  | target, 1 in 1000 | 30.7 us | +1.4 us |
  | both, 1 in 1000 | 31.5 us | +2.1 us |
  | both, every command | 34.7 us | +5.3 us |

  With `--kprobes`, both sides at 1 in 1000 added 3.8 us. Before the cuts
  of 2026-10-14 (per-packet stamps, io_work and `nvme_tcp_queue_rq` hooks),
  fentry added 2.9 us under the same conditions.

  When the CPUs are saturated, the per-I/O cost shows as lost throughput,
  and the sampling rate barely changes it. Same guest, with the initiator
  and the target sharing CPUs 4-7: fio, 4 KB random reads, 4 jobs x iodepth
  8, 3 runs of 12 s, 1 in 1000:

  | profiled | throughput | change |
  |---|---|---|
  | nothing | 145 kIOPS | |
  | initiator | 138 kIOPS | -5.0 % |
  | target | 133 kIOPS | -8.6 % |
  | both | 126 kIOPS | -12.9 % |
  | both, `--kprobes` | 112 kIOPS | -22.7 % |

  On two machines, each pays only its own side. A guest pays more per BPF
  program run than bare metal does; these numbers have not been measured
  on bare metal yet. For SPDK -> SPDK, measured before the cuts: 16.6 us,
  +1.3 us at 1 in 1000, +4.2 us with every command (section 8). At queue
  depth 32, SPDK kept 97 % of its IOPS at 1 in 1000.

## 8. SPDK

SPDK's NVMe/TCP initiator (`lib/nvme/nvme_tcp.c`) and target
(`lib/nvmf/tcp.c`) run in user space over kernel TCP sockets.
`spdk/ntprof-usdt-v26.05.patch` (about 110 lines in six files, and the
header `include/spdk_internal/ntprof.h`) lets ntprof see them.

**Times recorded by SPDK, one probe per command.** A USDT probe that a tracer
is attached to traps into the kernel on every hit, about a microsecond, as
long as a 4 KB command spends in some stages. A first version with a probe
at every stage boundary (16 per read) inflated the sampled commands by about
12 us. So SPDK records the times itself: for a sampled command, each stage
boundary is written into the command's `struct ntprof_times` with
`clock_gettime(CLOCK_MONOTONIC)` (the vDSO, tens of ns; the same clock as
`bpf_ktime_get_ns()`), and one probe per side fires when the command is done
on that side (`nvme_tcp_done` at the completion to the application,
`nvmf_tcp_done` at request state COMPLETED), handing over the times. The
probe comes after the command's last event, so its cost stays out of the
times.

| Event | Where in SPDK | Record |
|---|---|---|
| initiator SUBMIT | `nvme_tcp_qpair_submit_request` | `NTP_H_START`, `NTP_H_QUEUE_RQ` |
| CMD_SENT, H2C_SENT | socket layer: `sendmsg()` took the whole capsule / the last H2C PDU | `NTP_H_CMD_SENT`, `NTP_H_DATA_SENT` |
| R2T, C2H, RSP | the PDU header parsed | `NTP_H_R2T_PROC`, `NTP_H_C2H_PROC`; arrivals `NTP_H_*_ARR` |
| H2C_START | the first H2C data PDU queued | `NTP_H_DATA_START` |
| DONE | `nvme_tcp_req_complete` | `NTP_H_RSP_PROC`, `NTP_H_NVME_DONE`, `NTP_H_BLK_DONE` |
| target CMD | the command copied out of its capsule | `NTP_T_CMD_PROC`; arrival `NTP_T_CMD_ARR` |
| R2T_QUEUED, RSP_QUEUED | the first R2T / C2H data or response PDU queued | `NTP_T_R2T_QUEUED`, `NTP_T_RSP_QUEUED` |
| R2T_SENT, C2H_SENT, RSP_SENT | socket layer took the PDU | `NTP_T_R2T_SENT`, `NTP_T_C2H_SENT`, `NTP_T_RSP_SENT` (see below) |
| H2C | an H2C data header parsed | `NTP_T_DATA_PROC`; arrival `NTP_T_DATA_ARR` |
| EXEC, EXECUTED | request states EXECUTING, EXECUTED (bdev layer) | `NTP_T_EXEC`, `NTP_T_COMPLETE` |

"The socket layer took the PDU": the transports mark the socket request of a
sampled command's PDU (`internal.ntprof`); `spdk_sock_request_pend()`, which
runs when one `sendmsg()` has taken the whole request, then stores the time
in the request (`internal.ntprof_sent_ns`), and the transport files it when
the write completes. The write-completion callback itself is not the time:
with zero-copy send it waits for the kernel's notification, after the peer's
ACK.

**Sampling inside SPDK.** `ntprof_sampled()` applies `ntp_sample_hash()`
with the period in `g_ntprof_sample_every` (defined in `lib/sock/sock.c`, 0
by default). ntprof finds the processes that map the binary it was given,
resolves the variable's address from the ELF symbol table and the process's
mappings, and writes the period through `/proc/PID/mem` once its programs
are attached; targets first, so that a command a host samples is sampled by
its target too. It writes 0 when it stops, hosts first.

**What only the kernel sees.** Packet arrival and, on the target, the moment
a PDU was handed to TCP happen in the kernel. Hooks on `tcp_recvmsg()`,
`skb_copy_datagram_iter()` and `tcp_sendmsg()`, restricted to the SPDK
processes, keep per thread two rings (`bpf/spdk.bpf.h`): the start of each
`tcp_recvmsg()` that copied data with the receive timestamp of its first
skb, and the entries of `tcp_sendmsg()`. When the command is done, a
PDU parsed at time t gets the arrival of the latest read started before t,
and a target PDU taken by `sendmsg()` at time t gets the latest
`tcp_sendmsg()` entry before t, as its "sent" time (v1's try_send_* events;
the initiator's "sent" stays the return of `sendmsg()`, v1's done_send_req).
SPDK reads all queued bytes at once, so when one read carries several PDUs
the later ones take the first skb's time, which moves at most the spread of
that read from network time to receive-stack time. The rings are per CPU and
tagged with the thread: SPDK's reactors are pinned; a reactor that migrated
gets the parse time as arrival.

**Mapping to the model.** SPDK has no block layer: an initiator command
starts at submission and ends at its completion to the application, which
also plays the part of `blk_mq_complete_request_remote()` (`NTP_H_RSP_PROC`),
so S1 is 0 at both ends. The target's backend (bdev layer and device) is
S7-S9, without the block-layer split. Records carry `NTP_F_SPDK`; an
initiator's session is named `spdk-c<cntlid>n<nsid>`. Throughput of an SPDK
initiator is the sampled commands times the sampling period.

**Order.** The target's record comes when its request completes, which with
zero-copy send waits for the kernel's notification; on one machine the host's
record can come first (section 3).

**Limits.** The patch is for SPDK v26.05. Only the posix socket module was
tested (the uring module calls the same `spdk_sock_request_pend()`). SPDK's
NVMe/RDMA is not instrumented. Over loopback the receiver's stack runs inside
the sender's `sendmsg()`, so the network share can be slightly negative
(about -1 us for 4 KB writes); and a kernel target on the same machine runs
its `io_work` on the CPU the packet arrived on, which is the SPDK initiator's
polling reactor: it preempts the reactor, inflates the initiator's S2(S) and
turns the network share negative by as much. Neither happens across
machines.
