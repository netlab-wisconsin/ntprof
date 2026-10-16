# ntprof reference

ntprof breaks the latency of each NVMe-over-Fabrics I/O into the stages of its path: the host's block layer and
NVMe-oF driver, the network, the target's NVMe-oF driver, and the target's storage (its block layer and drive), on
the way down and back up. It also points at the stage that amplifies latency under load (LAD).
[The top-level README](../README.md) introduces it with an example. This file is the reference: building, commands,
options and output. [design.md](design.md) explains the hooks, the stage model and the accuracy limits.

ntprof runs on unmodified kernels: it attaches eBPF programs to tracepoints and functions that the kernel already
has, so it starts and stops on a running machine.

| | host (initiator) | target |
|---|---|---|
| Linux NVMe/TCP | `nvme-tcp` | `nvmet-tcp` |
| Linux NVMe/RDMA | `nvme-rdma` | `nvmet-rdma` |
| SPDK NVMe/TCP | `lib/nvme` (e.g. `spdk_nvme_perf`) | `nvmf_tgt` |
| SPDK NVMe/RDMA | `lib/nvme` (e.g. `spdk_nvme_perf`) | `nvmf_tgt` |

Linux: 5.15 and 6.x, tested on 6.8 and 6.11 and on two 5.15 hosts with MLNX OFED's NVMe modules (ConnectX-7,
Samsung PM9A3), no patch. SPDK: v26.05 with the probes from [`spdk/`](../spdk) (section
SPDK). A kernel host can be profiled against an SPDK target, and the other way round. Reads and writes of
any size are covered.

## Build

You need clang (12 or later), bpftool, the libelf and zlib development files, and the libbpf submodule (cloned
with `--recurse-submodules`, or fetched afterwards), all from the top of the repository:

```sh
git submodule update --init libbpf
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

1. the live breakdown of 4 KiB random reads, with host and target profiled together;
2. the two sides recorded separately, as on two machines, and joined offline;
3. the LAD per stage between queue depth 1 and 32, which names the stage that amplifies latency under load.

Each command is printed before it runs.

## Run

ntprof needs root (or `CAP_BPF` + `CAP_PERFMON`), and the NVMe-oF modules must be loaded. On the host,
connect first.

```sh
sudo ntprof record                      # the host, all namespaces, 1 in 1000 commands, Ctrl-C to stop
sudo ntprof record -r both -d 10        # host and target on this machine
sudo ntprof record -r target -o t.ntp   # on the target: start it first, stop it last ...
sudo ntprof record -o h.ntp -d 60       # ... and on the host, with the same -s
ntprof report h.ntp t.ntp               # the two sides joined
ntprof compare base.ntp loaded.ntp      # LAD per stage; a run is h.ntp or h.ntp,t.ntp
sudo ntprof record -t rdma              # NVMe/RDMA
sudo ntprof probe -r both               # which hooks this kernel offers
ntprof dump h.ntp > h.csv               # the raw records
```

| Option | Meaning |
|---|---|
| `-r host\|target\|both` | which side(s) to profile on this machine (default: host) |
| `-t tcp\|rdma` | the transport |
| `-s N` | record one command in N (default 1000; the ini file's `FREQUENCY`). Both sides pick the same commands, so use the same N on both |
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
| `-c FILE` | a task file in ini format ([`examples/ntprof.ini`](../examples/ntprof.ini) lists every key); command-line options override it |
| `-v` | more messages: the configuration, every counter (twice: libbpf's debug output) |

### Reading the output

While it records, `ntprof record` prints one line per group (I/O type, size, namespace) per interval:

```
                                                                          host                   target
    time  group                       n   cmds/s   mean us    p99 us   block  driver network  driver storage
    3.0s  read 4KiB nvme6n1         154    15.5k     73.40     80.73      1%      2%    98%*
```

`n` is the number of sampled commands, `cmds/s` the command rate estimated from them, and the last five
columns each stage's share of the mean latency. On two machines the host records without the target's records, so
its network column holds the network and the target together, marked `*`; with `-r both` on one machine, every
column is filled. The report that joins the two files has all five stages.

At the end, and with `ntprof report`, each group gets a summary:

```
== read 4KiB nvme6n1: 3066 commands sampled (from about 13.3k commands/s), all with the target's side
  latency (us): mean 72.77, p50 69.54, p99 80.30, p99.9 81.83
  stage                                   mean us  share   avg QD
  host block layer                           0.49     1%     0.01
  host driver (nvme-rdma)                    1.26     2%     0.02
  network (RDMA, NICs, wire)                10.03    14%     0.13  ###
  target driver (nvmet-rdma)                 1.02     1%     0.01
  storage (target block layer, drive)       59.97    82%     0.80  ####################
  largest: storage (target block layer, drive), 82 % of the latency
```

The stages add up to the end-to-end latency. `avg QD` is the mean number of commands in the stage (Little's
law: command rate × mean time). Without the target's records, the target's side is one row, "network and target
(not recorded)". Groups with under 1 % of the records are listed on one line (udev and partition scans make a few
such reads).

`--detail` adds the components of each stage, in path order, with p50 and p99:

```
  component                                     n      mean       L       p50       p99   (us)
  host block layer, submission               2213      0.20    0.00      0.20      0.26
  host driver, submission                    2213      2.05    0.02      2.04      2.33
  network: wire and NICs (computed)          2213     25.31    0.24     25.28     26.15
    data transfer (RDMA, at the target)      2213     15.78    0.15     15.77     16.13
  network: target receive path               2213      0.28    0.00      0.26      0.53
  target driver, submission                  2213      8.55    0.08      8.56      8.88
  storage: target block layer and drive      2213     39.01    0.38     38.72     41.92
    target block layer, submission           2213      2.02    0.02      2.02      2.16
    drive                                    2213     36.87    0.36     36.57     39.79
    target block layer, completion           2213      0.12    0.00      0.12      0.13
  target driver, completion                  2213      0.45    0.00      0.43      0.59
  network: host receive path                 2213      0.23    0.00      0.22      0.30
  host driver, completion                    2213      0.00    0.00      0.00      0.00
  host block layer, completion               2213      0.70    0.01      0.71      0.90
    handoff to nvme_complete_rq              2213      0.65    0.01      0.66      0.85
  end to end                                 2213     76.78    0.74     76.52     79.90
```

This is a 128 KiB write over NVMe/RDMA. The network's computed part is the host's wait minus the target's time:
the wire, the NICs, and the work neither driver sees. The receive paths run from a packet's arrival to the driver
handling it. Over NVMe/RDMA, "data transfer (RDMA, at the target)" is a write's RDMA READ, or a read's RDMA WRITE
and response until the send completion: the request's network stage, measured separately from its storage stage,
as a staged NIC-and-drive pipeline would time them ([design.md](design.md) §4).

A large NVMe/TCP read arrives in many packets, and the host copies each one while later ones are still on their
way. From the host's first copy of the data to the last data processed, `--detail` shows two parts: "host driver,
processing the data", the time `nvme-tcp` spent on the packets with the read's data, and "network: rest of the data
arriving", the rest of that time. On the two servers of [`use_cases/`](../use_cases), a 128 KiB read at queue
depth 1 arrived in about three packets over 30 µs; the host spent 19.7 µs processing them and waited 24.2 µs for
the rest of the data. Record files written before this split (file version 3) show the whole time as "host driver,
completion".

A large NVMe/TCP write after R2T is the same seen from the target: `nvmet-tcp` copies each packet of the write's
data into its buffer while later ones are still on their way. From the data's PDU header parsed to the end of its
last copy, `--detail` shows "target driver, processing the data", the time those copies took, and "network: rest of
the data arriving", the rest of that time. Record files written before this split (file version 4 and
older) show the whole time in "target driver, submission".

`ntprof compare` prints, per group present in both runs, each stage's mean in the baseline and under load, its
LAD (loaded ÷ baseline), and the stage with the largest LAD.

### Busy CPUs

Attaching an fentry/fexit program waits for every runnable thread to switch out voluntarily, and so does
detaching it. Fio jobs whose completions are always ready, or polling threads, can hold that wait up for as
long as they run ([design.md](design.md#hook-flavours)). ntprof attaches its programs in parallel, so the
waits overlap. It first checks, in a throwaway process, whether fentry attaches within 2 s, and uses kprobes
(which cost more per I/O) if not. If attaching still takes more than 10 s, it starts over with kprobes. Each
time, it says why. When it stops, it prints its report first. If removing the hooks then takes longer than
3 s, ntprof returns anyway and says that removal continues in the background. With `--fentry` there is no
fallback; a second Ctrl-C leaves a stuck attach behind.

## SPDK

SPDK runs NVMe/TCP and NVMe/RDMA in user space, where kernel hooks see nothing of it. ntprof uses USDT probes
instead, which [`spdk/ntprof-usdt-v26.05.patch`](../spdk/ntprof-usdt-v26.05.patch) adds to SPDK's NVMe/TCP and
NVMe/RDMA hosts and targets and to its socket layer:

```sh
cd spdk && git checkout v26.05
git apply /path/to/ntprof/spdk/ntprof-usdt-v26.05.patch
./configure --with-usdt --with-rdma && make
```

Start the SPDK applications first, then ntprof, naming the binary of each side that runs in SPDK:

```sh
sudo ntprof probe -r both --spdk-host spdk/build/bin/spdk_nvme_perf \
                          --spdk-target spdk/build/bin/nvmf_tgt
sudo ntprof record -r target --spdk-target spdk/build/bin/nvmf_tgt -o target.ntp
sudo ntprof record -r both -s 1 --spdk-host spdk/build/bin/spdk_nvme_perf \
                                --spdk-target spdk/build/bin/nvmf_tgt
sudo ntprof record -r both --spdk-target spdk/build/bin/nvmf_tgt   # kernel host
sudo ntprof record -r both -t rdma --spdk-host spdk/build/bin/spdk_nvme_perf \
                                   --spdk-target spdk/build/bin/nvmf_tgt
```

- ntprof profiles every running process that maps the named file. With SPDK's shared libraries, name the
  transport library first and add the socket module:
  `--spdk-target build/lib/libspdk_nvmf.so,build/lib/libspdk_sock_posix.so` (host:
  `libspdk_nvme.so,...`).
- SPDK samples nothing until ntprof, once attached, writes its sampling period into the processes. ntprof
  writes 0 back when it stops. An unsampled command costs SPDK one load and one compare. For a sampled
  command, SPDK itself writes the stage times (one vDSO clock read each), and one USDT probe per side fires
  when the command is done, after its last event, so the probe's cost (about a microsecond) stays out of the
  times. If ntprof is killed with SIGKILL, the period stays set (the probes are then no-ops) until the next
  run.
- `tests/demo-spdk.sh` (about a minute, no SSD or NIC) runs the steps on one machine: SPDK host → SPDK
  target, with ntprof's mean latency next to `spdk_nvme_perf`'s own; the same with 50 µs added in front of the
  target's bdev, where `ntprof compare` puts the delay in storage; and the kernel host against the SPDK
  target. `SPDK=` names the SPDK tree (default `~/spdk`); `tests/demo-spdk.sh down` cleans up.
- An SPDK host has no block layer: the host block layer is not shown, and the throughput line is estimated from the sampled
  commands.
- Over NVMe/RDMA on the two servers of [`use_cases/`](../use_cases), SPDK on both sides and the same drive (AIO
  bdev), a 4 KiB read at queue depth 1 took 68.2 µs: network 6.4 µs, host and target drivers 0.4 µs together,
  storage 61.4 µs, the drive's own time. With the Linux drivers on both sides it took 72.8 µs (network 10.0 µs,
  drivers 2.3 µs). ntprof at one command in 1000 added 0.11 µs.

## Kernels without BTF

fentry/fexit and the usual CO-RE flow need `/sys/kernel/btf/vmlinux`. If the kernel was built without
`CONFIG_DEBUG_INFO_BTF` but its modules carry DWARF (`CONFIG_DEBUG_INFO=y`), generate BTF from the modules
with pahole and pass it:

```sh
tools/gen-btf.sh /tmp/ntprof-btf     # nvme_tcp.btf, nvmet_tcp.btf, nvme_rdma.btf, nvmet_rdma.btf
sudo ntprof record --btf /tmp/ntprof-btf
```

ntprof then uses kprobes.

The same holds for NVMe modules built out of tree on a kernel that has BTF, such as MLNX OFED's DKMS packages:
the kernel has `/sys/kernel/btf/vmlinux`, but `/sys/kernel/btf/nvmet_tcp` (and the others) are missing, and the
installed modules are stripped. `ntprof probe` then shows "module BTF …: absent; kprobes". Build the same
source again with debug info, in a directory of your own (nothing is installed or loaded), and generate the BTF
from that build. For MLNX OFED, the DKMS source is in `/usr/src/mlnx-nvme-<version>`, and its `dkms.conf`
gives the make line:

```sh
cp -r /usr/src/mlnx-nvme-<version> ~/mlnx-nvme-dbg && cd ~/mlnx-nvme-dbg
make KVER=$(uname -r) K_BUILD=/lib/modules/$(uname -r)/build CONFIG_NVME_HOST_WITHOUT_FC=
modinfo -F srcversion host/nvme-tcp.ko; modinfo -F srcversion nvme_tcp   # the same source
mkdir ~/kos && cp host/nvme-tcp.ko host/nvme-rdma.ko target/nvmet-tcp.ko target/nvmet-rdma.ko ~/kos
tools/gen-btf.sh ~/ntprof-btf ~/kos
sudo ntprof record -r target --btf ~/ntprof-btf
```

Build it with the compiler the kernel was built with (`cat /proc/version`) when it is installed.

## Test beds

No SSD and no NIC is involved:

- `tests/loopback.sh tcp|rdma up [completion_ns] [io_queues]` exports a null_blk device through the kernel
  target and connects to it (stock modules, configfs). `tcp` runs over 127.0.0.1; `rdma` runs over soft-RoCE
  (`rdma_rxe`) bound to the default-route interface. `completion_ns` adds a fixed device latency.
  `tests/loopback.sh tcp|rdma down` removes everything. On two machines, `export [completion_ns]` sets up
  only the target side (at `ADDR` on `NETDEV`) and `connect ADDR [io_queues]` only the host side;
  `NULLB_MBPS` limits null_blk's bandwidth, a stand-in for a busy drive.
- `tests/spdk-target.sh up [delay_us]` starts an SPDK `nvmf_tgt` exporting a null bdev on 127.0.0.1:4421
  (reserving hugepages if needed); `delay_us` puts a delay bdev in front. `TRTYPE=rdma` with `ADDR` set to an
  RDMA interface's address serves NVMe/RDMA instead, and `AIO=/dev/...` exports that block device instead of
  the null bdev. `tests/spdk-target.sh trid` prints the transport ID for `spdk_nvme_perf -r`; `down` stops it.

## Layout

| Path | |
|---|---|
| `bpf/ntprof.h` | records, events, filter: shared by BPF and user space |
| `bpf/kernel_types.h` | the kernel types read, declared for CO-RE |
| `bpf/common.bpf.h` | maps and helpers of every BPF object |
| `bpf/host.bpf.h`, `bpf/target.bpf.h` | the block-layer and NVMe-core hooks of the kernel objects |
| `bpf/nvme_{tcp,rdma}_{host,target}.bpf.c` | the kernel transports' hooks |
| `bpf/spdk.bpf.h`, `bpf/spdk_{tcp,rdma}_{host,target}.bpf.c` | SPDK's probes |
| `spdk/` | the SPDK patch that adds the probes |
| `src/loader.c`, `src/spdk.c` | BTF and flavour choice, hook selection, attach, ring buffer; SPDK processes |
| `src/record.c`, `src/offline.c` | `record`; `report`, `compare`, `dump` |
| `src/model.c` | the stage model |
| `src/join.c`, `src/stats.c`, `src/report.c` | join, aggregation, output |
| `tests/` | unit tests, test beds, the demos (`demo.sh` kernel, `demo-spdk.sh` SPDK) |
| `tools/gen-btf.sh` | BTF from module DWARF |
| `examples/ntprof.ini` | a task file for `-c` |
| `use_cases/` | four questions answered on real hardware |
| `docs/` | this reference and the design |
| `libbpf/` | the libbpf submodule |
