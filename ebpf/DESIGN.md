# ntprof v2 design

ntprof v2 measures the same thing as the NSDI'25 ntprof (v1, `src/` and
`kernel_patches/` in this repository): the latency of each NVMe-over-Fabrics
I/O, split into the stages of the path from the initiator's block layer to the
target's device and back (S1-S9 in the paper's Figure 4). v1 did it with 23
tracepoints patched into `nvme-tcp` / `nvmet-tcp`, a patched `blk-core.c` and
two kernel modules. v2 does it with eBPF on unmodified kernels.

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
| BLK_SUBMIT (`rq->start_time_ns`) | same field, read at `nvme_tcp_queue_rq` | `NTP_H_START` |
| NVME_TCP_QUEUE_RQ (after `blk_mq_start_request`) | entry of `nvme_tcp_queue_rq`; `raw_tp/nvme_setup_cmd` for cid, opcode, LBA | `NTP_H_QUEUE_RQ` |
| NVME_TCP_DONE_SEND_REQ (`nvme_tcp_done_send_req`, inlined) | exit of `nvme_tcp_try_send_{cmd_pdu,data,ddgst}` returning 1 with `queue->request` cleared | `NTP_H_CMD_SENT`, `NTP_H_DATA_SENT` |
| (none) | entry of `nvme_tcp_try_send_data_pdu` after an R2T | `NTP_H_DATA_START` |
| NVME_TCP_RECV_SKB (`skb->tstamp`, socket timestamps enabled by the patch) | `raw_tp/netif_receive_skb` stamps each skb; looked up at `nvme_tcp_recv_skb` | `NTP_H_*_ARR` |
| NVME_TCP_HANDLE_C2H_DATA (inlined) | first `skb_copy_datagram_iter` while `queue->pdu` holds a C2H header | `NTP_H_C2H_PROC` |
| NVME_TCP_HANDLE_R2T (inlined on 6.11) | `nvme_tcp_setup_h2c_data_pdu` while receiving | `NTP_H_R2T_PROC` |
| NVME_TCP_PROCESS_NVME_CQE (inlined) | `blk_mq_complete_request_remote` while receiving | `NTP_H_RSP_PROC` |
| (none) | `raw_tp/nvme_complete_rq` | `NTP_H_NVME_DONE` |
| BLK_RQ_COMPLETE | `raw_tp/block_rq_complete` | `NTP_H_BLK_DONE` |

### Target (`bpf/nvme_tcp_target.bpf.c`)

| v1 event | v2 hook | v2 event |
|---|---|---|
| NVMET_TCP_TRY_RECV_PDU (`SO_TIMESTAMP` of `kernel_recvmsg`) | arrival stamp of the last skb copied in `nvmet_tcp_io_work` before the command | `NTP_T_CMD_ARR` |
| NVMET_TCP_DONE_RECV_PDU (inlined) | `raw_tp/nvmet_req_init` | `NTP_T_CMD_PROC` |
| (R2T queued) | `nvmet_tcp_queue_response` while data is still due | `NTP_T_R2T_QUEUED` |
| NVMET_TCP_TRY_SEND_R2T (inlined) | `sock_sendmsg` in `nvmet_tcp_io_work`, `queue->snd_cmd->state == SEND_R2T`, offset 0 | `NTP_T_R2T_SENT` |
| NVMET_TCP_HANDLE_H2C_DATA_PDU (inlined) | `nvmet_tcp_build_pdu_iovec` after an R2T | `NTP_T_DATA_*` |
| NVMET_TCP_EXEC_{READ,WRITE}_REQ | `nvmet_{bdev,file}_execute_rw`, `nvmet_passthru_execute_cmd` | `NTP_T_EXEC` |
| (none) | `raw_tp/block_rq_issue` / `block_rq_complete` of the backing request (`bio->bi_private` is the `nvmet_req`) | `NTP_T_BE_ISSUE`, `NTP_T_BE_DONE` |
| (none) | `raw_tp/nvmet_req_complete` | `NTP_T_COMPLETE` |
| NVMET_TCP_QUEUE_RESPONSE | `nvmet_tcp_queue_response` for the completion | `NTP_T_RSP_QUEUED` |
| (none) | `sock_sendmsg`, state `SEND_DATA_PDU` | `NTP_T_C2H_SENT` |
| NVMET_TCP_TRY_SEND_RESPONSE (inlined) | `sock_sendmsg`, state `SEND_RESPONSE` | `NTP_T_RSP_SENT` |

### Hook flavours

Every function hook exists twice with the same body: as fentry/fexit
(`*_fe`, `*_fx`) and as kprobe/kretprobe (`*_kp`, `*_kr`). The loader keeps
fentry/fexit when the kernel has BTF (cheaper, and fexit sees the return value
without remembering the arguments) and kprobes otherwise or with `--kprobes`.
Attaching fentry programs is slow (about 5 s for both sides on the
development guest; each trampoline update waits for an RCU grace period); kprobes
attach in about 1 s.

A program whose attach point is missing on the running kernel is not loaded;
`ntprof probe` lists them. The records then lack that event, and the model
falls back where it can (section 4).

## 3. Records and their join

Each side keeps a record per sampled command in a BPF hash map and emits it
on a ring buffer when the command leaves that side: the target at its
response, the host at `block_rq_complete` (`bpf/ntprof.h`). Records carry
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
is sent) is always consumed before its host record. Across machines, the two
record files are joined offline (`ntprof report host.ntp target.ntp`).

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

## 5. Queue occupancy

v1 had the `ENABLE_QUEUE_LENGTH` switch but did not compute queue lengths.
v2 reports, per component, the mean number of commands in it by Little's law:
L = (command rate) x (mean time in the component), the rate being the group's
records times the sampling period over the time they span. This costs
nothing at run time and is exact for means in steady state.

`--exact-queues` (ini `QUEUE_TRACKING=exact`) additionally counts, per
NVMe/TCP queue, the commands not yet sent and not yet completed (host) and
not yet answered (target), as seen by each arriving command. It needs map
operations on every I/O, sampled or not.

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

- **Packet arrival.** v1 used `skb->tstamp` (socket timestamps). v2 stamps
  each skb at `netif_receive_skb`, after GRO, in a direct-mapped table. When
  TCP coalesces a later skb into an earlier one in the receive queue, the
  earlier skb's stamp is used (arrival appears earlier). A table collision or
  an skb address reused within a second falls back to the time of reading.
- **Context.** Per-thread context lives in per-CPU slots tagged with the
  thread id; if a thread migrates between a function's entry and the hook
  that uses the context, the event is lost, not misattributed.
- **Not covered:** NVMe/TCP with TLS (`nvme connect --tls`), polled-mode
  completions on the target, admin commands, and commands other than read and
  write. Data digests are covered on the host (`skb_copy_and_hash_datagram_iter`).
- **Overhead.** The hooks run for every I/O; sampling only decides which
  commands get a record. Measured overhead: experiment
  `2026-10-13_ntprof-v2-ebpf` in the research workspace.
