# ntprof v2: eBPF profiler for NVMe-over-Fabrics

ntprof breaks the latency of each NVMe-over-Fabrics I/O into the stages of its
path, from the initiator's block layer through the transport, the network and
the target's transport and block layer to the device and back, and points at
the stage that amplifies latency under load (LAD). This is version 2 of the
tool from

> Yuyuan Kang and Ming Liu. *Understanding and Profiling NVMe-over-TCP Using
> ntprof.* NSDI 2025.

v1 (`../src`, `../kernel_patches`) needed a patched and rebooted Linux 5.15.
v2 runs on unmodified kernels: it attaches eBPF programs to tracepoints and
functions the kernel already has, so it can be started and stopped on a
running machine. [DESIGN.md](DESIGN.md) explains the hooks, the stage model
and the accuracy limits.

Supported: NVMe/TCP, initiator (`nvme-tcp`) and target (`nvmet-tcp`), reads
and writes, on Linux 6.x (tested on 6.8 and 6.11).

## Build

Needs clang (12 or later), bpftool, libelf and zlib development files, and the
libbpf submodule:

```sh
git submodule update --init ebpf/libbpf
cd ebpf
make                    # builds ./ntprof
make test               # unit tests (no root, no BPF)
```

`make BPFTOOL=/path/to/bpftool` if `bpftool` is not on the PATH (Ubuntu's
`/usr/sbin/bpftool` refuses to run on a kernel it has no package for; use
`/usr/lib/linux-tools/<any version>/bpftool` instead).

## Run

ntprof needs root (or `CAP_BPF` + `CAP_PERFMON`). The NVMe/TCP modules must be
loaded: on the initiator, connect first.

```sh
sudo ./ntprof probe -r both          # which hooks this kernel offers
sudo ./ntprof record                 # initiator, all namespaces, 1 in 1000 commands
sudo ./ntprof record -r target -o target.ntp          # on the target
sudo ./ntprof record -r host --session nvme1n1 -s 100 -o host.ntp -d 60
./ntprof report host.ntp target.ntp  # join the two sides offline
sudo ./ntprof record -r both -s 1    # initiator and target on one machine
./ntprof compare base.ntp loaded.ntp # LAD per stage between two runs
./ntprof dump host.ntp > host.csv    # raw records
```

| Option | Meaning |
|---|---|
| `-r host\|target\|both` | which side(s) to profile on this machine |
| `-s N` | record one command in N (v1: `FREQUENCY`); both sides pick the same commands |
| `-i SEC`, `-d SEC` | online report interval; total duration (default: until Ctrl-C) |
| `-o FILE`, `-j FILE` | save the records; write the final report as JSON |
| `--session nvmeXnY` | profile one namespace (all its paths with native multipath) |
| `--io-type`, `--size` | filter by read/write and exact sizes (`4K,128K`) |
| `--exact-queues` | count queue occupancy per request (costs map operations per I/O) |
| `--kprobes` | use kprobes even where fentry/fexit are available |
| `--btf DIR` | BTF generated with `tools/gen-btf.sh`, for kernels built without BTF |
| `-c FILE` | a v1 task specification (`../src/ntprof_config.ini`); options override it |

### Reading the report

```
== read 4096B nvme0n1: 18354 records, 18354 joined with the target
  component                            stage              n      mean       L       p50       p99   (us)
  blk-mq submission                    S1(S)          18354      0.27    0.01      0.26      0.39
  initiator transport submission       S2(S)          18354     12.13    0.29     11.87     19.33
  network and unaccounted stack time   S3-S5(S+C)     18354      3.02    0.07      3.02      7.30
  target receive stack                 S5(S)          18354      4.99    0.12      4.87     10.00
  ...
  end to end                           S1-S9          18354     29.53    0.71     28.93     38.21
  stages (mean us, share of end to end): S1 0.75 (3%) S2 12.79 (43%) S3-S5 11.74 (40%) S6 2.29 (8%) S7-S9 1.96 (7%)
```

Components are in path order and add up to the end-to-end time. `L` is the
mean number of commands in the component (Little's law). Without the target's
records the network and the target appear as one component (S3-S9).

LAD (latency amplification degree) of a stage is its mean latency under load
divided by its mean latency in an unloaded baseline run; `ntprof compare`
prints it per stage and names the stage with the largest LAD.

## Kernels without BTF

fentry/fexit and the usual CO-RE flow need `/sys/kernel/btf/vmlinux`. If the
kernel was built without `CONFIG_DEBUG_INFO_BTF` but its modules carry DWARF
(`CONFIG_DEBUG_INFO=y`), generate BTF from the modules with pahole and pass it:

```sh
tools/gen-btf.sh /tmp/ntprof-btf          # writes nvme_tcp.btf, nvmet_tcp.btf
sudo ./ntprof record --btf /tmp/ntprof-btf
```

ntprof then uses kprobes.

## Test bed

`tests/loopback-tcp.sh up [completion_ns] [queues]` exports a null_blk device
through the kernel target on 127.0.0.1 and connects to it (stock modules,
configfs; no SSD, no NIC). `completion_ns` adds a fixed device latency.
`tests/loopback-tcp.sh down` removes everything.

## Layout

| Path | |
|---|---|
| `bpf/ntprof.h` | records, events, filter: shared by BPF and user space |
| `bpf/kernel_types.h` | the kernel types read, declared for CO-RE |
| `bpf/common.bpf.h` | maps and helpers of every BPF object |
| `bpf/nvme_tcp_host.bpf.c`, `bpf/nvme_tcp_target.bpf.c` | the hooks |
| `src/loader.c` | BTF choice, hook selection, attach, ring buffer |
| `src/record.c`, `src/offline.c` | `record`; `report`, `compare`, `dump` |
| `src/model.c` | the stage model |
| `src/join.c`, `src/stats.c`, `src/report.c` | join, aggregation, output |
| `tests/` | unit tests, loopback test bed |
| `tools/gen-btf.sh` | BTF from module DWARF |
