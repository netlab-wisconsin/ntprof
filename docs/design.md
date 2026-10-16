# ntprof design

ntprof measures the latency of each NVMe-over-Fabrics I/O, split into the
stages of its path from the host's block layer to the target's drive and
back, with eBPF on unmodified kernels for NVMe/TCP and NVMe/RDMA, and with
USDT probes for SPDK's user-space NVMe/TCP (section 8). The stage model is
the one of the NSDI '25 paper (section 4). This document explains how ntprof
gets there.

## 1. Overview

| | |
|---|---|
| Instrumentation | stock tracepoints, raw tracepoints, fentry/fexit or kprobes (section 2) |
| Kernel | any kernel with BPF; tested on 5.15 (MLNX OFED), 6.8 and 6.11 |
| Host and target | each side records its own timestamps; user space joins the two records of a command, and nothing is added to the PDUs (section 3) |
| Sampling | both sides hash (cid, slba, nlb) and keep the same one command in N |
| Analysis | in user space, online or from record files |
| Output | text, JSON, raw records (CSV via `dump`), LAD between runs |
| Transports | NVMe/TCP and NVMe/RDMA in the kernel, host and target; SPDK NVMe/TCP, host and target |

## 2. Hooks

A hook is used only where the kernel already has an attach point: a
tracepoint, or a function the compiler kept out of line. The NVMe/TCP
drivers' own steps are mostly `static` functions and often inlined (on 6.11 `nvme_tcp_queue_request`,
`nvme_tcp_done_send_req`, `nvme_tcp_handle_c2h_data`, `nvme_tcp_handle_r2t`,
`nvme_tcp_process_nvme_cqe` and all of `nvmet-tcp`'s send functions are), so ntprof
watches the functions around them, or exported kernel functions they call,
and tells the NVMe/TCP calls apart with a per-thread context.

### Host (`bpf/nvme_tcp_host.bpf.c`)

| hook | event |
|---|---|
| the request's `rq->start_time_ns`, read at `nvme_setup_cmd` | `NTP_H_START` |
| `raw_tp/nvme_setup_cmd` (cid, opcode, LBA), inside `nvme_tcp_queue_rq`; the entry of `nvme_tcp_queue_rq` itself only with `--exact-queues` | `NTP_H_QUEUE_RQ` |
| exit of `nvme_tcp_try_send_{cmd_pdu,data,ddgst}` returning 1 with `queue->request` cleared | `NTP_H_CMD_SENT`, `NTP_H_DATA_SENT` |
| entry of `nvme_tcp_try_send_data_pdu` after an R2T | `NTP_H_DATA_START` |
| where the three `try_send` functions are inlined: `nvme_tcp_try_send` returning 1 with `queue->request` cleared; the request is `queue->request` at its entry or at its first `kernel_sendpage` / `sock_no_sendpage` / `sock_sendmsg` | `NTP_H_CMD_SENT`, `NTP_H_DATA_SENT`, `NTP_H_DATA_START` |
| `skb->tstamp` read at `nvme_tcp_recv_skb`; ntprof turns receive timestamps on (section 7) | `NTP_H_*_ARR` |
| first `skb_copy_datagram_iter` while `queue->pdu` holds a C2H header | `NTP_H_C2H_PROC` |
| every such copy of the read's data: the arrival of its skb | `NTP_H_C2H_LAST_ARR` |
| exit of each `nvme_tcp_recv_skb` that copied the read's data, or its completion inside the call: the call's time on the data (section 4) | `NTP_H_C2H_DONE`, `c2h_busy_ns` |
| `nvme_tcp_setup_h2c_data_pdu` while receiving | `NTP_H_R2T_PROC` |
| `blk_mq_complete_request_remote` while receiving | `NTP_H_RSP_PROC` |
| `raw_tp/nvme_complete_rq` | `NTP_H_NVME_DONE` |
| `raw_tp/block_rq_complete` | `NTP_H_BLK_DONE` |

### Target (`bpf/nvme_tcp_target.bpf.c`)

| hook | event |
|---|---|
| `skb->tstamp` of the last skb this thread copied (`skb_copy_datagram_iter`) from the command's queue socket | `NTP_T_CMD_ARR` |
| `raw_tp/nvmet_req_init` | `NTP_T_CMD_PROC` |
| `nvmet_tcp_queue_response` while data is still due | `NTP_T_R2T_QUEUED` |
| `sock_sendmsg` (`kernel_sendpage` up to 6.4) on a queue socket of a sampled command, `queue->snd_cmd->state == SEND_R2T`, offset 0 | `NTP_T_R2T_SENT` |
| `nvmet_tcp_build_pdu_iovec` after an R2T | `NTP_T_DATA_*` |
| exit of `skb_copy_datagram_iter` on a queue socket receiving a sampled write's data after R2T (`rcv_state == RECV_DATA`, `queue->cmd` the write) | `NTP_T_DATA_DONE`, `NTP_T_DATA_LAST_ARR`, the copy's time |
| `nvmet_{bdev,file}_execute_rw`, `nvmet_passthru_execute_cmd` | `NTP_T_EXEC` |
| `raw_tp/block_rq_issue` / `block_rq_complete` of the backing request (`bio->bi_private` is the `nvmet_req`) | `NTP_T_BE_ISSUE`, `NTP_T_BE_DONE` |
| `raw_tp/nvmet_req_complete` | `NTP_T_COMPLETE` |
| `nvmet_tcp_queue_response` for the completion | `NTP_T_RSP_QUEUED` |
| `sock_sendmsg`, state `SEND_DATA_PDU` | `NTP_T_C2H_SENT` |
| `sock_sendmsg`, state `SEND_RESPONSE` | `NTP_T_RSP_SENT` |

**Linux 5.15 and MLNX OFED** (two hosts, 2026-10-15). Up to 6.4, and in
OFED's backport for 5.15, both drivers send with `kernel_sendpage()`, which
does not go through `sock_sendmsg()`; the target hooks both (a send takes
one path or the other). The OFED `nvme-tcp` on 5.15 inlines all three
`try_send` functions into `nvme_tcp_try_send()`, which stays out of line: the
loader then attaches the fallback in the host table instead, and only
then, since `kernel_sendpage()` and `sock_sendmsg()` run for every socket.

Telling NVMe/TCP apart without a hook on its entry points: `nvme_setup_cmd`
and `nvmet_req_init` run for every NVMe transport. The host follows a
request's driver PDU to its queue and the target follows the `nvmet_sq` to
the queue that embeds it. Either way, the command is NVMe/TCP's when the
queue's socket has `sk_user_data` pointing back to that queue. Both drivers
set this when they take over a socket. For another transport, the same
offsets hold unrelated data and the test fails. The target's `sock_sendmsg`
hook uses the same link (socket to queue) and runs only while a sampled
command is in flight.

### NVMe/RDMA (`bpf/nvme_rdma_host.bpf.c`, `bpf/nvme_rdma_target.bpf.c`)

The events keep the TCP names where the meaning carries over ("sent" = posted to the queue pair). The block-layer and
nvme-core hooks are the same as for TCP (`bpf/host.bpf.h`,
`bpf/target.bpf.h`).

| Side | hook | event |
|---|---|---|
| host | entry of `nvme_rdma_queue_rq` | `NTP_H_QUEUE_RQ` |
| host | `nvme_rdma_post_send` returning 0 | `NTP_H_CMD_SENT` |
| host | `nvme_rdma_send_done` | `NTP_H_SEND_DONE` |
| host | `nvme_rdma_recv_done` (the response's CQE; cid from the CQE) | `NTP_H_RSP_ARR` |
| host | `blk_mq_complete_request_remote` in a completion handler | `NTP_H_RSP_PROC` |
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
both hold, and kprobes otherwise or with `--kprobes`. The choice is made per
object: an object whose module has no BTF of its own (a module built out of
tree, such as MLNX OFED's DKMS builds) uses kprobes even when the kernel has
BTF.

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
sides compute the same hash, so they sample the same commands without any
change to the PDUs.

**Order.** Within one process (`--role both`) the host and target programs
share one ring buffer, so a command's target record (emitted when its response
is sent) is normally consumed before its host record. With SPDK the host can
complete first (section 8); a host record without its target record then
waits up to 100 ms in the joiner before it counts as unmatched. Across
machines, the two record files are joined offline (`ntprof report host.ntp
target.ntp`), which is order-independent.

**Arming.** Programs attach one by one. Nothing is recorded or counted until
every program is attached; a command that started earlier would miss events.

**A missed last event.** The target finds a command's record by its
`nvmet_req`, which the transport hands out again once the response is sent.
If the event that emits the record was missed, the next command on the same
`nvmet_req` would write its times into it. So `nvmet_req_init` emits a record
that is still open (flag `NTP_F_REUSED`) before anything of the new command
can reach it. Seen on gnode5 (NVMe/RDMA, kprobes, 4 KiB reads at queue depth
32): `nvmet_rdma_queue_response`'s hook did not run for 10 % of the commands.
The model then takes `nvmet_req_complete`'s time for the posting when the
send completion was seen (section 4), and `ntprof report` counts such records.

## 4. The stage model (`src/model.c`)

Notation: H = host timestamp, T = target timestamp, as in `bpf/ntprof.h`.
For each round trip between the host and the target, the host waits from
handing its PDU to TCP until the target's answer reaches its network stack;
the target is busy from the arrival of the host's PDU until its answer is
handed to TCP. The **network** share of the round trip is the host's wait minus
the target's residence: wire, NICs and the TCP/IP work neither side's
transport sees (the NSDI '25 paper's model). Over loopback it is about 0 and
can be slightly negative (the target may see a packet before the host's
`sendmsg` returns).

| Component, as reported | Read | Write, data in the capsule | Write, data after R2T |
|---|---|---|---|
| host block layer, submission | H_QUEUE_RQ - H_START | same | same |
| host driver, submission | H_CMD_SENT - H_QUEUE_RQ | same | + (H_DATA_START - H_R2T_PROC) |
| network: wire and NICs (computed) | (H_C2H_ARR - H_CMD_SENT) - (T_C2H_SENT - T_CMD_ARR) | (H_RSP_ARR - H_CMD_SENT) - (T_RSP_SENT - T_CMD_ARR) | two round trips: (H_R2T_ARR - H_CMD_SENT) - (T_R2T_SENT - T_CMD_ARR) + (H_RSP_ARR - H_DATA_START) - (T_RSP_SENT - T_DATA_ARR) |
| network: target receive path | T_CMD_PROC - T_CMD_ARR | same | + (T_DATA_PROC - T_DATA_ARR) |
| target driver, submission | T_EXEC - T_CMD_PROC | same (includes receiving the data) | (T_R2T_SENT - T_CMD_PROC) + (T_EXEC - T_DATA_DONE) |
| network: rest of the data arriving at the target | - | - | (T_DATA_DONE - T_DATA_PROC) - busy |
| target driver, processing the data | - | - | busy |
| storage: target block layer and drive | T_COMPLETE - T_EXEC | same | same |
| target driver, completion | T_C2H_SENT - T_COMPLETE | T_RSP_SENT - T_COMPLETE | same |
| network: host receive path | H_C2H_PROC - H_C2H_ARR | H_RSP_PROC - H_RSP_ARR | + (H_R2T_PROC - H_R2T_ARR) |
| network: rest of the data arriving | (H_C2H_DONE - H_C2H_PROC) - busy | - | - |
| host driver, processing the data | busy | - | - |
| host driver, completion | H_RSP_PROC - H_C2H_DONE | 0 | 0 |
| host block layer, completion | H_BLK_DONE - H_RSP_PROC | same | same |

The components add up to the end-to-end latency H_BLK_DONE - H_START exactly
(tested in `tests/test_model.c`). Details split a component further: storage
into the target's block layer and the drive (bdev back end only), and the
host block layer's completion into the handoff to `nvme_complete_rq` (the CQE
parsed, then the completion IPI). The five stages are sums of components, and
they are what LAD is computed on:

| Stage | Components | The paper's name |
|---|---|---|
| host block layer | both host block layer components | S1 |
| host driver | the host driver components | S2 |
| network | the computed part, both receive paths, and a TCP transfer's wait for the rest of its data | S3-S5 |
| target driver | the target driver components | S6 |
| storage | target block layer and drive | S7-S9 (the drive alone: S8-S9) |

For a write after R2T the second round trip starts when the host *starts*
sending the data: the target receives the data while the host is still
sending it, so the transfer belongs to both the host's wait and the target's
residence and cancels out of the network share.

**A TCP read's data phase.** A large read's data comes in many skbs, and
nvme-tcp copies each one as it is read while later ones are still on their
way, so receiving and copying overlap. The host times its own work on the
read's data: every `nvme_tcp_recv_skb` call that copies it, from the first
copy on, until the call ends or the read completes inside it. These calls
come one after the other from the queue's `io_work` and all lie between the
first copy (H_C2H_PROC) and the end of the last one (H_C2H_DONE). Their sum is
*busy*, the host driver processing the data; the rest of that span is time
the host had nothing of the read's to process: it was waiting for the data.
The wait is never negative, and a read whose copying never pauses has none.
Records without the data phase (file version 3 and older, SPDK) keep the whole
span in the completion: H_RSP_PROC - H_C2H_PROC. The arrival of the read's last
data (H_C2H_LAST_ARR) is recorded too, for the overlap of the two.

**A TCP write's data phase** is the same on the target, for a write whose
data follows an R2T. nvmet-tcp receives the data with `sock_recvmsg()` from
functions that are inlined into `nvmet_tcp_io_work()` on every kernel checked
(5.15 with MLNX OFED, 6.8, 6.11), so the target times the copies themselves:
each `skb_copy_datagram_iter()` that moves the write's data into the
command's buffer. The copy that starts inside the skb that carried the PDU
header counts from the header's parsing (T_DATA_PROC), since that data was
already there; every other copy counts from its own start. The copies follow
each other between T_DATA_PROC and the end of the last one (T_DATA_DONE);
their sum is *busy*, the target driver processing the data, and the rest of
that span is the target waiting for data. TCP's own work between the copies
(freeing skbs, ACKs, the socket backlog) is in the wait, as it is on the host,
where `tcp_read_sock()` does the same work between `nvme_tcp_recv_skb()`
calls. What follows the last copy, a data digest's CRC and check and the
hand-over to the back end, stays in the target driver's submission. Records
without the data phase (file version 4 and older, SPDK) keep the whole span
T_EXEC - T_DATA_PROC there. T_DATA_LAST_ARR is the arrival of the write's last
data.

Without a target record (host only, or a target record that misses events)
the network and target components are reported together as "network and
target (no target record)".

**NVMe/RDMA.** The host CPU sees one round trip: the command posted, the
response's CQE polled; the target moves the data by RDMA WRITE (reads) or
RDMA READ (writes) without the host CPU. So the host's wait is H_RSP_ARR -
H_CMD_SENT, its receive path H_RSP_PROC - H_RSP_ARR, and the host driver's completion is 0. On the
target, a write's RDMA READ (T_DATA_PROC - T_R2T_SENT) is wire and remote-NIC
time seen from the target: it counts as network, not as target residence.
The target driver's submission is then (T_R2T_SENT - T_CMD_PROC) + (T_EXEC - T_DATA_PROC). When the
posting (T_RSP_SENT) was not seen but the send completion was, T_COMPLETE
stands in for it: the response is queued right after `nvmet_req_complete`,
and the target driver's completion is under a microsecond when it is seen. A posting later than its
own send completion, or a drive completion later than T_COMPLETE, belongs to
another command and is ignored.

The detail component "data transfer (RDMA, at the target)", under the network, is
the time the data spends crossing the network as the target sees it: a
write's RDMA READ (T_DATA_PROC - T_R2T_SENT), and a read's RDMA WRITE with
its response, from posting to the send completion (T_SEND_DONE -
T_C2H_SENT). These are the spans a staged pipeline times as its NIC stage:
an RDMA READ into host memory before a drive write, or an RDMA WRITE after a
drive read. With storage, the drive stage, one NVMe/RDMA run then shows both
stages of each request. A read's span includes the host NIC's
acknowledgement on its way back, which the host's wait does not, so it
can exceed the read's share of the network. A write whose data fits in the command
(inline, 4 KiB by default) has no such span.

## 5. Queue occupancy

ntprof reports, per component, the mean number of commands in it by Little's law:
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
kernels run the kprobe flavour (fentry needs real kernel BTF). Modules built
out of tree on a kernel with BTF are the same case: their BTF comes from a
debug build of the same source ([reference](reference.md#kernels-without-btf)), checked by
`srcversion`.

## 7. Accuracy and its limits

- **Packet arrival.** The arrival of a PDU is the receive
  timestamp of its skb (`skb->tstamp`). The kernel stamps every received
  packet (`net_timestamp_check()`, after GRO) while any socket asks for
  receive timestamps; ntprof holds a UDP socket with
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
- **A TCP read's data phase** (section 4). The wait is what the host saw: it
  holds the wire and NICs, both TCP/IP stacks, the target's time to hand the
  rest of the data to TCP, and any time `io_work` waited for a CPU while data
  was ready. The busy time is wall-clock time of the calls, so an interrupt
  that runs the receive stack on the same CPU during a call counts as busy.
  At queue depth above 1 a call can also carry another command's data after
  the read's, and counts in full.
- **A TCP write's data phase** (section 4), likewise: the wait holds the host's
  time to hand the data to TCP, the wire and NICs, both TCP/IP stacks and
  TCP's work on the target between copies. A write sent as several H2C data
  PDUs (over 4 MiB, nvmet-tcp's limit, or a host with a smaller one) has its
  later PDU headers, and with a data digest each earlier PDU's CRC, inside the
  span: they count as wait. The exit hook runs for every copy on the target,
  a kretprobe where the target uses kprobes; `NTPROF_SKIP=ntp_t_copy_done`
  turns it off, and write records then keep the old formula.
- **Not covered:** NVMe/TCP with TLS (`nvme connect --tls`), polled-mode
  completions on the target, admin commands, and commands other than read and
  write. Data digests are covered on the host (`skb_copy_and_hash_datagram_iter`).
- **Overhead.** The kernel hooks run for every I/O. Sampling only decides
  which commands get a record. The hooks that only add timestamps return
  after one array lookup while no sampled command is in flight. What is left
  at 1 in 1000 is mostly the fixed cost of a BPF program run at each hook
  that every command passes: the sampling decision (`nvme_setup_cmd`,
  `nvmet_req_init`), the target's copy hook, which notes every skb it
  copies, and the host's receive hooks, which return at once. Measured in a
  16-vCPU guest with host and target together over loopback (Linux 6.8,
  fentry, null_blk, 2 I/O queues, 2026-10-14): fio, 4 KB random reads at
  queue depth 1 on one CPU, mean of 3 runs of 8 s:

  | profiled | mean latency | added |
  |---|---|---|
  | nothing | 29.4 us | |
  | host, 1 in 1000 | 30.3 us | +0.9 us |
  | target, 1 in 1000 | 30.7 us | +1.4 us |
  | both, 1 in 1000 | 31.5 us | +2.1 us |
  | both, every command | 34.7 us | +5.3 us |

  With `--kprobes`, both sides at 1 in 1000 added 3.8 us. Before the cuts
  of 2026-10-14 (per-packet stamps, io_work and `nvme_tcp_queue_rq` hooks),
  fentry added 2.9 us under the same conditions.

  When the CPUs are saturated, the per-I/O cost shows as lost throughput,
  and the sampling rate barely changes it. Same guest, with the host
  and the target sharing CPUs 4-7: fio, 4 KB random reads, 4 jobs x iodepth
  8, 3 runs of 12 s, 1 in 1000:

  | profiled | throughput | change |
  |---|---|---|
  | nothing | 145 kIOPS | |
  | host | 138 kIOPS | -5.0 % |
  | target | 133 kIOPS | -8.6 % |
  | both | 126 kIOPS | -12.9 % |
  | both, `--kprobes` | 112 kIOPS | -22.7 % |

  On two machines, each pays only its own side. On two servers (Linux 5.15
  with MLNX OFED, ConnectX-7 400 GbE, a PM9A3 behind the kernel target, the
  host on fentry and the target on kprobes, 2026-10-15), both sides at 1 in
  1000 added 1.3 us to an 87.1 us NVMe/TCP read at queue depth 1 and 0.8 us to
  a 75.0 us NVMe/RDMA read, and cost 6.4 % (TCP) and 2.0 % (RDMA) of the
  IOPS of 4 jobs x queue depth 32. For SPDK -> SPDK, measured before the cuts: 16.6 us,
  +1.3 us at 1 in 1000, +4.2 us with every command (section 8). At queue
  depth 32, SPDK kept 97 % of its IOPS at 1 in 1000.

## 8. SPDK

SPDK's NVMe/TCP host (`lib/nvme/nvme_tcp.c`) and target
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
| host SUBMIT | `nvme_tcp_qpair_submit_request` | `NTP_H_START`, `NTP_H_QUEUE_RQ` |
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
`tcp_sendmsg()` entry before t, as its "sent" time (the host's "sent"
stays the return of `sendmsg()`).
SPDK reads all queued bytes at once, so when one read carries several PDUs
the later ones take the first skb's time, which moves at most the spread of
that read from network time to receive-stack time. The rings are per CPU and
tagged with the thread: SPDK's reactors are pinned; a reactor that migrated
gets the parse time as arrival.

**Mapping to the model.** SPDK has no block layer: a host command
starts at submission and ends at its completion to the application, which
also plays the part of `blk_mq_complete_request_remote()` (`NTP_H_RSP_PROC`),
so the host block layer is 0 at both ends. The target's backend (bdev layer and device) is
storage, without the block-layer split. Records carry `NTP_F_SPDK`; a
host's session is named `spdk-c<cntlid>n<nsid>`. Throughput of an SPDK
host is the sampled commands times the sampling period.

**Order.** The target's record comes when its request completes, which with
zero-copy send waits for the kernel's notification; on one machine the host's
record can come first (section 3).

**Limits.** The patch is for SPDK v26.05. Only the posix socket module was
tested (the uring module calls the same `spdk_sock_request_pend()`). SPDK's
NVMe/RDMA is not instrumented. Over loopback the receiver's stack runs inside
the sender's `sendmsg()`, so the network share can be slightly negative
(about -1 us for 4 KB writes); and a kernel target on the same machine runs
its `io_work` on the CPU the packet arrived on, which is the SPDK host's
polling reactor: it preempts the reactor, inflates the host driver's submission and
turns the network share negative by as much. Neither happens across
machines.
