# ntprof

**Where does the time of an NVMe-over-Fabrics I/O go?** ntprof answers this one stage at a time. For a sample of
the reads and writes an application issues to a remote NVMe namespace, it measures the time each I/O spends in
the initiator's block layer, the initiator's transport, the network and the TCP/IP stacks, the target's
transport, and the target's block layer and device. It reports the share of each stage and how many commands
wait in it. Comparing two runs, it shows which stage amplifies latency under load.

ntprof comes from the NSDI'25 paper [*Understanding and Profiling NVMe-over-TCP Using
ntprof*](https://www.usenix.org/conference/nsdi25/presentation/kang) ([project
website](https://ntprof.cs.wisc.edu/)). This repository holds two versions:

| | v2, [`ebpf/`](ebpf/README.md) (use this one) | v1, [`src/`](src/README.md) (the paper's artifact) |
|---|---|---|
| Kernel | any Linux 6.x with BTF, unmodified, no reboot | Linux 5.15, patched with new tracepoints, rebuilt, rebooted |
| How | eBPF programs on existing tracepoints and functions | two kernel modules |
| Transports | NVMe/TCP, NVMe/RDMA, SPDK NVMe/TCP (initiator and target, mixed too) | NVMe/TCP |
| Output | live per-interval lines, a summary per I/O group, JSON, raw records (CSV), LAD between runs | text |

## How it works

The paper models the I/O path as a lossless network of software switches. Each stage is a queue, and a
command's latency is the sum of the time it spends in each one:

```
            initiator                                              target
 app -> [ S1 blk-mq ] -> [ S2 nvme-tcp ] -> S3-S5 network -> [ S6 nvmet-tcp ] -> [ S7 blk-mq ] -> [ S8-S9 SSD ]
     <-       S1      <-       S2       <-      S3-S5      <-        S6        <-       S7      <-
```

ntprof v2 attaches eBPF programs to tracepoints and functions that the kernel already has (`nvme_setup_cmd`,
`block_rq_complete`, the nvme-tcp send and receive paths, and so on). Each side records a sampled command's
timestamps on that machine only. Both sides pick the same commands without exchanging anything: each hashes
(command id, LBA, length) and keeps one command in N. ntprof then joins each command's initiator and target
records in user space and turns the timestamps into stage times. SPDK runs NVMe/TCP in user space, where the
kernel hooks see nothing, so for SPDK a small patch adds USDT probes ([`ebpf/spdk`](ebpf/spdk)).

## Quick start

On Linux 6.x with BTF (tested on 6.8 and 6.11), with clang 12 or later, bpftool, and the libelf and zlib
development files:

```bash
git clone --recurse-submodules -b v2 https://github.com/netlab-wisconsin/ntprof.git
cd ntprof/ebpf
make && sudo make install          # /usr/local/sbin/ntprof
```

The initiator must already be connected (`nvme connect ...`). Profile both sides of a running workload:

```bash
# on the target: start first, stop last
sudo ntprof record -r target -o target.ntp -d 40
# on the initiator
sudo ntprof record -o host.ntp -d 30
# anywhere, with both files
ntprof report host.ntp target.ntp
```

When the initiator and the target are on one machine, `sudo ntprof record -r both -d 10` does it all in one
command. `ntprof record` alone profiles only the initiator's side and prints the target's side as one stage.
To try ntprof without an SSD or a NIC, run `ebpf/tests/demo.sh`: it exports a null_blk device over loopback
NVMe/TCP and walks through the commands below in two minutes.

## What a report looks like

4 KiB random reads at queue depth 1, recorded on both sides, one command in 10 (the demo's test bed: a 16-vCPU
VM, Linux 6.8, kernel initiator and target over 127.0.0.1, null_blk):

```
== read 4KiB nvme0n1: 23640 commands sampled (from about 30.3k commands/s), all with the target's side
  latency (us): mean 25.86, p50 25.39, p99 34.19, p99.9 41.86
  stage  where the time goes                   mean us  share   avg QD
  S1     initiator block layer (blk-mq)           1.14     4%     0.03  #
  S2     initiator transport (nvme-tcp)          11.51    45%     0.35  ###########
  S3-S5  network and TCP/IP stacks                9.24    36%     0.28  #########
  S6     target transport (nvmet-tcp)             2.26     9%     0.07  ##
  S7-S9  target block layer and device            1.71     7%     0.05  ##
  largest: S2, initiator transport (nvme-tcp), 45 % of the latency
```

Reports are grouped by I/O type, size and namespace. `avg QD` is the mean number of commands in the stage
(Little's law). `--detail` splits the stages into their components (the receive stacks, the device, the
completion path). While recording, ntprof prints one line per group every second.

## Use case: which stage breaks under load?

A workload slows down when its queue depth goes up. Is the SSD saturated, or is it the fabric, and which part?
Record the same workload at low and high load and compare: the latency amplification degree (LAD) of a stage is
its mean latency loaded divided by its mean latency unloaded.

```bash
sudo ntprof record -r both -s 10 -d 8 --offline -o qd1.ntp     # while fio runs at iodepth=1
sudo ntprof record -r both -s 10 -d 8 --offline -o qd32.ntp    # while fio runs at iodepth=32
ntprof compare qd1.ntp qd32.ntp
```

```
== read 4KiB nvme0n1: baseline 23225 commands sampled, loaded 93441
  stage      where the time goes                    base us   load us     LAD
  S1         initiator block layer (blk-mq)            1.08      0.72    0.67
  S2         initiator transport (nvme-tcp)           11.56     67.73    5.86
  S3-S5      network and TCP/IP stacks                 9.00    144.62   16.06
  S6         target transport (nvmet-tcp)              2.25     16.86    7.50
  S7-S9      target block layer and device             1.74      1.02    0.58
  S1-S9      end to end                               25.64    230.95    9.01
  largest LAD: S3-S5, network and TCP/IP stacks, 16.06
```

On this test bed, the end-to-end latency grows 9× from queue depth 1 to 32, and the device is not the reason:
S7-S9 does not grow at all. The time goes to the TCP/IP stacks (16×) and to both transports (6× and 7.5×).
`ntprof report qd32.ntp` shows where the 32 commands wait: 17.6 of them, on average, are in S3-S5. A single fio
job on one CPU submits every command here, so they all share one nvme-tcp queue and one TCP connection. Spreading
the load over more CPUs and queues is the thing to try. A faster SSD would not help.

## What it costs

The hooks on the I/O path run for every command, and ntprof records one command in 1000 by default
(`-s N`). Sampled or not, every command passes the hooks, so the cost depends little on N. Measured in the
same 16-vCPU VM (Linux 6.8, 2026-10-14):

| workload | not profiled | initiator profiled | target profiled | both profiled |
|---|---|---|---|---|
| 4 KiB read, queue depth 1, mean latency | 29.4 us | +0.9 us | +1.4 us | +2.1 us |
| 4 KiB read, 4 jobs × iodepth 8 on 4 saturated CPUs | 145 kIOPS | −5.0 % | −8.6 % | −12.9 % |

Initiator and target shared the CPUs here. On two machines, each machine pays only for its own side. These
numbers come from a VM, where each eBPF program run costs more than on bare metal. Details, including kprobes
and SPDK, are in [`ebpf/DESIGN.md`](ebpf/DESIGN.md#7-accuracy-and-its-limits).

## Documentation

- [`ebpf/README.md`](ebpf/README.md): building, every command and option, SPDK, kernels without BTF, the test
  beds.
- [`ebpf/DESIGN.md`](ebpf/DESIGN.md): the hooks and how they map to the paper's tracepoints, the stage model,
  the join, accuracy limits and overhead.
- [`src/README.md`](src/README.md): v1, the NSDI'25 artifact, with [`kernel_patches/`](kernel_patches/README.md)
  and [`use_cases/`](use_cases) (run on CloudLab with v1).

## Citation

```bibtex
@inproceedings{kang2025ntprof,
  author    = {Yuyuan Kang and Ming Liu},
  title     = {Understanding and Profiling {NVMe-over-TCP} Using ntprof},
  booktitle = {22nd USENIX Symposium on Networked Systems Design and Implementation (NSDI 25)},
  year      = {2025},
  pages     = {1117--1136},
  publisher = {USENIX Association},
}
```
