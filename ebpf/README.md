# ntprof v2: eBPF profiler for NVMe-over-Fabrics

ntprof breaks the latency of each NVMe-over-Fabrics I/O into the stages of its path, from the initiator's block
layer through the transport, the network and the target's transport and block layer to the device and back. It
also points at the stage that amplifies latency under load (LAD). [The top-level README](../README.md)
introduces it with an example. This file is the reference: building, commands, options and output.
[DESIGN.md](DESIGN.md) explains the hooks, the stage model and the accuracy limits.

v1 (`../src`, `../kernel_patches`) needed a patched and rebooted Linux 5.15. v2 runs on unmodified kernels: it
attaches eBPF programs to tracepoints and functions that the kernel already has, so it starts and stops on a
running machine.

| | initiator | target |
|---|---|---|
| Linux NVMe/TCP | `nvme-tcp` | `nvmet-tcp` |
| Linux NVMe/RDMA | `nvme-rdma` | `nvmet-rdma` |
| SPDK NVMe/TCP | `lib/nvme` (e.g. `spdk_nvme_perf`) | `nvmf_tgt` |

Linux: 6.x, tested on 6.8 and 6.11, no patch. SPDK: v26.05 with the probes from [`spdk/`](spdk) (section
SPDK). A kernel initiator can be profiled against an SPDK target, and the other way round. Reads and writes of
any size are covered.

## Build

You need clang (12 or later), bpftool, the libelf and zlib development files, and the libbpf submodule:

```sh
git submodule update --init ebpf/libbpf
cd ebpf
make                    # builds ./ntprof, one binary with the BPF programs inside
make test               # unit tests (no root, no BPF)
sudo make install       # /usr/local/sbin/ntprof (PREFIX=, DESTDIR= as usual)
./ntprof --version
```

If `bpftool` is not on the PATH, use `make BPFTOOL=/path/to/bpftool`. Ubuntu's `/usr/sbin/bpftool` refuses
to run on a kernel it has no package for; use `/usr/lib/linux-tools/<any version>/bpftool` instead. The
bpftool of Ubuntu 22.04 (v5.15) is too old for ntprof's objects ("Error finalizing .BTF"); build a current one
(`git clone --recurse-submodules -b v7.4.0 https://github.com/libbpf/bpftool && make -C bpftool/src`). At run
time the binary needs only libelf and zlib.

## Try it

On any Linux 6.x machine with the stock `nvme-tcp`, `nvmet-tcp` and `null_blk` modules, fio and nvme-cli (no
SSD or NIC is used):

```sh
make
tests/demo.sh          # about two minutes; tests/demo.sh down removes the test bed
```

The demo exports a null_blk device through the kernel target on 127.0.0.1 and connects to it. It then shows:

1. the live breakdown of 4 KiB random reads, with initiator and target profiled together;
2. the two sides recorded separately, as on two machines, and joined offline;
3. the LAD per stage between queue depth 1 and 32, which names the stage that amplifies latency under load.

Each command is printed before it runs.

## Run

ntprof needs root (or `CAP_BPF` + `CAP_PERFMON`), and the NVMe-oF modules must be loaded. On the initiator,
connect first.

```sh
sudo ntprof record                      # initiator side, all namespaces, 1 in 1000 commands, Ctrl-C to stop
sudo ntprof record -r both -d 10        # initiator and target on this machine
sudo ntprof record -r target -o t.ntp   # on the target: start it first, stop it last ...
sudo ntprof record -o h.ntp -d 60       # ... and on the initiator, with the same -s
ntprof report h.ntp t.ntp               # the two sides joined
ntprof compare base.ntp loaded.ntp      # LAD per stage; a run is h.ntp or h.ntp,t.ntp
sudo ntprof record -t rdma              # NVMe/RDMA
sudo ntprof probe -r both               # which hooks this kernel offers
ntprof dump h.ntp > h.csv               # the raw records
```

| Option | Meaning |
|---|---|
| `-r host\|target\|both` | which side(s) to profile on this machine (default: host, the initiator) |
| `-t tcp\|rdma` | the transport |
| `-s N` | record one command in N (default 1000; v1: `FREQUENCY`). Both sides pick the same commands, so use the same N on both |
| `-d SEC` | stop after SEC seconds (default: Ctrl-C) |
| `-i SEC`, `--offline` | one line per group every SEC seconds (default 1); none |
| `-o FILE`, `-j FILE` | save the records; write the final report as JSON |
| `--session nvmeXnY` | profile one namespace (all its paths with native multipath) |
| `--io-type`, `--size` | only reads or writes; only these sizes (`4K,128K`) |
| `--detail` | the components of each stage too |
| `--all-groups` | also groups with under 1 % of the records (one line by default) |
| `--aggregation avg\|min\|max\|dist` | the statistics printed (`dist`: all quantiles) |
| `--exact-queues` | count queue lengths per command (costs map operations on every I/O) |
| `--kprobes`, `--fentry` | force a hook flavour (default: fentry/fexit when they can attach now) |
| `--btf DIR` | BTF generated with `tools/gen-btf.sh`, for kernels built without BTF |
| `--spdk-host PATH`, `--spdk-target PATH` | that side runs in SPDK (section SPDK) |
| `-c FILE` | a v1 task specification (`../src/ntprof_config.ini`); command-line options override it |
| `-v` | more messages: the configuration, every counter (twice: libbpf's debug output) |

### Reading the output

While it records, `ntprof record` prints one line per group (I/O type, size, namespace) per interval:

```
    time  group                       n   cmds/s   mean us    p99 us     S1    S2 S3-S5    S6 S7-S9
    5.1s  read 4KiB nvme0n1       15381    30.4k     25.73     34.01    4%   45%   35%    9%    7%
```

`n` is the number of sampled commands, `cmds/s` the command rate estimated from them, and the last five
columns each stage's share of the mean latency. Without the target's records, the S3-S5 column holds S3 to S9,
marked `*`.

At the end, and with `ntprof report`, each group gets a summary:

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

The stages add up to the end-to-end latency. `avg QD` is the mean number of commands in the stage (Little's
law: command rate × mean time). Without the target's records, the target's side is one row, S3-S9 "network
and target". Groups with under 1 % of the records are listed on one line (udev and partition scans make a few
such reads). `--detail` adds a table of components: each stage split into its parts (S3-S5 into the two
receive stacks and the network, S7-S9 into the target's block layer and the device), with p50 and p99.
Over NVMe/RDMA it also shows "data transfer (RDMA, at the target)": a write's RDMA READ, or a read's RDMA
WRITE and response until the send completion. That is the request's network stage, measured separately from
its drive stage (S7-S9), as a staged NIC-and-drive pipeline would time them (DESIGN.md §4).

`ntprof compare` prints, per group present in both runs, each stage's mean in the baseline and under load, its
LAD (loaded ÷ baseline), and the stage with the largest LAD.

### Busy CPUs

Attaching an fentry/fexit program waits for every runnable thread to switch out voluntarily, and so does
detaching it. Fio jobs whose completions are always ready, or polling threads, can hold that wait up for as
long as they run ([DESIGN.md](DESIGN.md#hook-flavours)). ntprof attaches its programs in parallel, so the
waits overlap. It first checks, in a throwaway process, whether fentry attaches within 2 s, and uses kprobes
(which cost more per I/O) if not. If attaching still takes more than 10 s, it starts over with kprobes. Each
time, it says why. When it stops, it prints its report first. If removing the hooks then takes longer than
3 s, ntprof returns anyway and says that removal continues in the background. With `--fentry` there is no
fallback; a second Ctrl-C leaves a stuck attach behind.

## SPDK

SPDK runs NVMe/TCP in user space, where kernel hooks see nothing of it. ntprof uses USDT probes instead, which
[`spdk/ntprof-usdt-v26.05.patch`](spdk/ntprof-usdt-v26.05.patch) adds to SPDK's NVMe/TCP initiator, target and
socket layer:

```sh
cd spdk && git checkout v26.05
git apply /path/to/ntprof/ebpf/spdk/ntprof-usdt-v26.05.patch
./configure --with-usdt && make
```

Start the SPDK applications first, then ntprof, naming the binary of each side that runs in SPDK:

```sh
sudo ntprof probe -r both --spdk-host spdk/build/bin/spdk_nvme_perf \
                          --spdk-target spdk/build/bin/nvmf_tgt
sudo ntprof record -r target --spdk-target spdk/build/bin/nvmf_tgt -o target.ntp
sudo ntprof record -r both -s 1 --spdk-host spdk/build/bin/spdk_nvme_perf \
                                --spdk-target spdk/build/bin/nvmf_tgt
sudo ntprof record -r both --spdk-target spdk/build/bin/nvmf_tgt   # kernel initiator
```

- ntprof profiles every running process that maps the named file. With SPDK's shared libraries, name the
  transport library first and add the socket module:
  `--spdk-target build/lib/libspdk_nvmf.so,build/lib/libspdk_sock_posix.so` (initiator:
  `libspdk_nvme.so,...`).
- SPDK samples nothing until ntprof, once attached, writes its sampling period into the processes. ntprof
  writes 0 back when it stops. An unsampled command costs SPDK one load and one compare. For a sampled
  command, SPDK itself writes the stage times (one vDSO clock read each), and one USDT probe per side fires
  when the command is done, after its last event, so the probe's cost (about a microsecond) stays out of the
  times. If ntprof is killed with SIGKILL, the period stays set (the probes are then no-ops) until the next
  run.
- `tests/demo-spdk.sh` (about a minute, no SSD or NIC) runs the steps on one machine: SPDK initiator → SPDK
  target, with ntprof's mean latency next to `spdk_nvme_perf`'s own; the same with 50 µs added in front of the
  target's bdev, where `ntprof compare` puts the delay in S7-S9; and the kernel initiator against the SPDK
  target. `SPDK=` names the SPDK tree (default `~/spdk`); `tests/demo-spdk.sh down` cleans up.
- An SPDK initiator has no block layer: S1 is not shown, and the throughput line is estimated from the sampled
  commands.

## Kernels without BTF

fentry/fexit and the usual CO-RE flow need `/sys/kernel/btf/vmlinux`. If the kernel was built without
`CONFIG_DEBUG_INFO_BTF` but its modules carry DWARF (`CONFIG_DEBUG_INFO=y`), generate BTF from the modules
with pahole and pass it:

```sh
tools/gen-btf.sh /tmp/ntprof-btf     # nvme_tcp.btf, nvmet_tcp.btf, nvme_rdma.btf, nvmet_rdma.btf
sudo ntprof record --btf /tmp/ntprof-btf
```

ntprof then uses kprobes.

## Test beds

No SSD and no NIC is involved:

- `tests/loopback.sh tcp|rdma up [completion_ns] [io_queues]` exports a null_blk device through the kernel
  target and connects to it (stock modules, configfs). `tcp` runs over 127.0.0.1; `rdma` runs over soft-RoCE
  (`rdma_rxe`) bound to the default-route interface. `completion_ns` adds a fixed device latency.
  `tests/loopback.sh tcp|rdma down` removes everything. On two machines, `export [completion_ns]` sets up
  only the target side (at `ADDR` on `NETDEV`) and `connect ADDR [io_queues]` only the initiator side;
  `NULLB_MBPS` limits null_blk's bandwidth, a stand-in for a busy drive.
- `tests/spdk-target.sh up [delay_us]` starts an SPDK `nvmf_tgt` exporting a null bdev on 127.0.0.1:4421
  (reserving hugepages if needed); `delay_us` puts a delay bdev in front. `tests/spdk-target.sh trid` prints
  the transport ID for `spdk_nvme_perf -r`; `down` stops it.

## Layout

| Path | |
|---|---|
| `bpf/ntprof.h` | records, events, filter: shared by BPF and user space |
| `bpf/kernel_types.h` | the kernel types read, declared for CO-RE |
| `bpf/common.bpf.h` | maps and helpers of every BPF object |
| `bpf/host.bpf.h`, `bpf/target.bpf.h` | the block-layer and NVMe-core hooks of the kernel objects |
| `bpf/nvme_{tcp,rdma}_{host,target}.bpf.c` | the kernel transports' hooks |
| `bpf/spdk.bpf.h`, `bpf/spdk_tcp_{host,target}.bpf.c` | SPDK's probes |
| `spdk/` | the SPDK patch that adds the probes |
| `src/loader.c`, `src/spdk.c` | BTF and flavour choice, hook selection, attach, ring buffer; SPDK processes |
| `src/record.c`, `src/offline.c` | `record`; `report`, `compare`, `dump` |
| `src/model.c` | the stage model |
| `src/join.c`, `src/stats.c`, `src/report.c` | join, aggregation, output |
| `tests/` | unit tests, test beds, the demos (`demo.sh` kernel, `demo-spdk.sh` SPDK) |
| `tools/gen-btf.sh` | BTF from module DWARF |
