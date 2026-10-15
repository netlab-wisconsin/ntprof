# ntprof

**Where does the time of an NVMe-over-Fabrics I/O go?** ntprof answers this one stage at a time. For a sample of
the reads and writes an application sends to a remote NVMe namespace, it measures the time each I/O spends in the
host's block layer and NVMe-oF driver, in the network, in the target's NVMe-oF driver, and in the target's storage
(its block layer and drive). It reports each stage's share of the latency and how many commands wait in it, and
comparing two runs, it names the stage that slows down under load.

ntprof runs on unmodified Linux kernels: it attaches eBPF programs to tracepoints and functions the kernel already
has, so it starts and stops on a running machine. It profiles the host (initiator) and the target of Linux NVMe/TCP
and NVMe/RDMA, and SPDK's NVMe/TCP, and joins the two sides of every sampled command.

It comes from the NSDI '25 paper [*Understanding and Profiling NVMe-over-TCP Using
ntprof*](https://www.usenix.org/conference/nsdi25/presentation/kang). Project website:
[ntprof.cs.wisc.edu](https://ntprof.cs.wisc.edu/).

## What a report looks like

4 KiB random reads over NVMe/RDMA at queue depth 1, from one server to another (ConnectX-7 400 GbE, a Samsung
PM9A3 behind the kernel's NVMe target), both sides recorded and joined:

```
== read 4KiB nvme5n1: 2979 commands sampled (from about 13.0k commands/s), all with the target's side
  latency (us): mean 72.92, p50 69.68, p99 80.43, p99.9 81.52
  stage                                   mean us  share   avg QD
  host block layer                           0.50     1%     0.01
  host driver (nvme-rdma)                    1.26     2%     0.02
  network (RDMA, NICs, wire)                10.01    14%     0.13  ###
  target driver (nvmet-rdma)                 1.04     1%     0.01
  storage (target block layer, drive)       60.11    82%     0.78  ####################
  largest: storage (target block layer, drive), 82 % of the latency
```

The stages add up to the end-to-end latency, and `avg QD` is the mean number of commands in each (Little's law).
Over NVMe/TCP the same read spends 19.9 µs in the network instead of 10.0 µs. `--detail` splits each stage into its
parts: the receive paths, the drive itself, and over NVMe/RDMA, each request's data transfer.

## The stages

```
           host                                                          target
 app -> [ block layer ] -> [ NVMe-oF driver ] -> network -> [ NVMe-oF driver ] -> [ block layer ] -> [ drive ]
     <-                <-                    <-          <-                    <-                <-
```

| Stage | What it covers |
|---|---|
| host block layer | blk-mq on the host, on the way down and back up |
| host driver | `nvme-tcp`, `nvme-rdma` or SPDK's NVMe/TCP initiator: sending the command, and receiving a TCP read's data |
| network | the NICs, the wire, and the TCP/IP or RDMA receive paths on both machines |
| target driver | `nvmet-tcp`, `nvmet-rdma` or SPDK's `nvmf_tgt`: parsing the command, fetching a write's data, sending the response |
| storage | the target's block layer and the drive |

The NSDI '25 paper names the same stages S1 (host block layer), S2 (host driver), S3–S5 (network), S6 (target
driver) and S7–S9 (storage).

## Quick start

On Linux 5.15 or 6.x with BTF, with clang 12 or later, bpftool, and the libelf and zlib development files:

```bash
git clone --recurse-submodules -b v2 https://github.com/netlab-wisconsin/ntprof.git
cd ntprof/ebpf
make && sudo make install          # /usr/local/sbin/ntprof
sudo ntprof probe -r both          # which hooks this kernel offers
```

The host must already be connected (`nvme connect ...`). Profile both sides of a running workload:

```bash
# on the target: start first, stop last
sudo ntprof record -r target -o target.ntp
# on the host
sudo ntprof record -o host.ntp -d 30
# anywhere, with both files
ntprof report host.ntp target.ntp
```

`-t rdma` profiles NVMe/RDMA. When the host and the target are one machine, `sudo ntprof record -r both -d 10` does
it all; `ntprof record` alone profiles only the host and reports the network and the target together. To try ntprof
without an SSD or a NIC, run `ebpf/tests/demo.sh`: it exports a null_blk device over loopback NVMe/TCP and walks
through recording, joining and comparing in two minutes.

## Use case: which stage breaks under load?

A workload slows down as its queue depth grows. Record the same workload at queue depth 1 and 32 and compare. A
stage's latency amplification degree (LAD) is its mean latency under load divided by its mean latency at the
baseline:

```bash
ntprof compare qd1/host.ntp,qd1/target.ntp qd32/host.ntp,qd32/target.ntp
```

```
== read 4KiB nvme5n1: baseline 2540 commands sampled, loaded 2208
  stage                                    base us   load us     LAD
  host block layer                            0.52      0.55    1.06
  host driver (nvme-tcp)                      3.77      4.63    1.23
  network (TCP/IP, NICs, wire)               19.83    187.57    9.46
  target driver (nvmet-tcp)                   2.45     16.67    6.80
  storage (target block layer, drive)        59.98     66.78    1.11
  end to end                                 86.56    276.21    3.19
  largest LAD: network (TCP/IP, NICs, wire), 9.46
```

Over NVMe/TCP, one fio job's 4 KiB reads get 3.2× slower at queue depth 32, and the drive is not the reason (1.1×):
the time goes to the network path (9.5×) and the target's driver (6.8×). One job submits every command on one CPU,
so they share one NVMe/TCP queue and one TCP connection; spreading the load over more CPUs and queues is the thing to
try. [`use_cases/`](use_cases) has this and three more cases from the same servers: a busy drive, a busy network, and
the network and drive time of each NVMe/RDMA request.

## What it costs

The hooks run for every command, and ntprof records one command in 1000 by default (`-s N`). Measured on the two
servers above (Linux 5.15, both sides recording, three runs each):

| 4 KiB random read | NVMe/TCP | NVMe/RDMA |
|---|---|---|
| queue depth 1, mean latency | 86.8 → 88.1 µs (+1.3 µs) | 75.1 → 75.8 µs (+0.8 µs) |
| 4 jobs × queue depth 32, throughput | 384.2k → 363.3k IOPS (−5.5 %) | 730.3k → 715.3k IOPS (−2.1 %) |

Each machine pays only for its own side.

## Documentation

- [`ebpf/README.md`](ebpf/README.md): building, every command and option, reading the output, SPDK, kernels and
  modules without BTF, the test beds.
- [`ebpf/DESIGN.md`](ebpf/DESIGN.md): the hooks, the sampling and the join, the stage model, accuracy and overhead.
- [`use_cases/`](use_cases): four questions answered on real hardware.
- The paper's original implementation (kernel modules and a patched Linux 5.15) is kept for reproducing the paper:
  [`src/README.md`](src/README.md), [`kernel_patches/`](kernel_patches/README.md) and its use cases in
  [`src/use_cases/`](src/use_cases). ntprof reads its ini task files with `-c`.

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
