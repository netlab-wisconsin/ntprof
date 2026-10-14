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

| | initiator | target |
|---|---|---|
| Linux NVMe/TCP | `nvme-tcp` | `nvmet-tcp` |
| Linux NVMe/RDMA | `nvme-rdma` | `nvmet-rdma` |
| SPDK NVMe/TCP | `lib/nvme` (e.g. `spdk_nvme_perf`) | `nvmf_tgt` |

Linux: 6.x, tested on 6.8 and 6.11, no patch. SPDK: v26.05 with the probes of
[`spdk/`](spdk) (section SPDK). A kernel initiator can be profiled against an
SPDK target and the other way round. Reads and writes; all I/O sizes.

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

ntprof needs root (or `CAP_BPF` + `CAP_PERFMON`). The NVMe-oF modules must be
loaded: on the initiator, connect first.

```sh
sudo ./ntprof probe -r both          # which hooks this kernel offers
sudo ./ntprof record                 # initiator, all namespaces, 1 in 1000 commands
sudo ./ntprof record -r target -o target.ntp          # on the target
sudo ./ntprof record -r host --session nvme1n1 -s 100 -o host.ntp -d 60
sudo ./ntprof record -t rdma         # NVMe/RDMA
./ntprof report host.ntp target.ntp  # join the two sides offline
sudo ./ntprof record -r both -s 1    # initiator and target on one machine
./ntprof compare base.ntp loaded.ntp # LAD per stage between two runs
./ntprof dump host.ntp > host.csv    # raw records
```

| Option | Meaning |
|---|---|
| `-r host\|target\|both` | which side(s) to profile on this machine |
| `-t tcp\|rdma` | the transport |
| `-s N` | record one command in N (v1: `FREQUENCY`); both sides pick the same commands |
| `-i SEC`, `-d SEC` | online report interval; total duration (default: until Ctrl-C) |
| `-o FILE`, `-j FILE` | save the records; write the final report as JSON |
| `--session nvmeXnY` | profile one namespace (all its paths with native multipath) |
| `--io-type`, `--size` | filter by read/write and exact sizes (`4K,128K`) |
| `--exact-queues` | count queue occupancy per request (costs map operations per I/O) |
| `--kprobes` | use kprobes even where fentry/fexit are available |
| `--btf DIR` | BTF generated with `tools/gen-btf.sh`, for kernels built without BTF |
| `--spdk-host PATH`, `--spdk-target PATH` | that side runs in SPDK (section SPDK) |
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

## SPDK

SPDK runs NVMe/TCP in user space, where kernel hooks see nothing of it.
ntprof uses USDT probes instead, which
[`spdk/ntprof-usdt-v26.05.patch`](spdk/ntprof-usdt-v26.05.patch) adds to
SPDK's NVMe/TCP initiator, target and socket layer:

```sh
cd spdk && git checkout v26.05
git apply /path/to/ntprof/ebpf/spdk/ntprof-usdt-v26.05.patch
./configure --with-usdt && make
```

Start the SPDK applications first, then ntprof, naming the binary of each
side that runs in SPDK:

```sh
sudo ./ntprof probe -r both --spdk-host spdk/build/bin/spdk_nvme_perf \
                            --spdk-target spdk/build/bin/nvmf_tgt
sudo ./ntprof record -r target --spdk-target spdk/build/bin/nvmf_tgt -o target.ntp
sudo ./ntprof record -r both -s 1 --spdk-host spdk/build/bin/spdk_nvme_perf \
                                  --spdk-target spdk/build/bin/nvmf_tgt
sudo ./ntprof record -r both --spdk-target spdk/build/bin/nvmf_tgt   # kernel initiator
```

- ntprof profiles every running process that maps the named file. With
  SPDK's shared libraries, name the transport library first and add the
  socket module: `--spdk-target build/lib/libspdk_nvmf.so,build/lib/libspdk_sock_posix.so`
  (initiator: `libspdk_nvme.so,...`).
- SPDK samples nothing until ntprof, once attached, writes its sampling
  period into the processes; it writes 0 back when it stops. An unsampled
  command costs SPDK a load and a compare. A sampled one has its stage times
  written by SPDK itself (a vDSO clock read each) and one USDT probe per side
  when it is done, after its last event, so the probe's cost (about a
  microsecond) stays out of the times. If ntprof is killed with SIGKILL, the
  period stays set (the probes are then no-ops) until the next run.
- An SPDK initiator has no block layer: S1 is empty, and the throughput line
  is estimated from the sampled commands.

## Kernels without BTF

fentry/fexit and the usual CO-RE flow need `/sys/kernel/btf/vmlinux`. If the
kernel was built without `CONFIG_DEBUG_INFO_BTF` but its modules carry DWARF
(`CONFIG_DEBUG_INFO=y`), generate BTF from the modules with pahole and pass it:

```sh
tools/gen-btf.sh /tmp/ntprof-btf     # nvme_tcp.btf, nvmet_tcp.btf, nvme_rdma.btf, nvmet_rdma.btf
sudo ./ntprof record --btf /tmp/ntprof-btf
```

ntprof then uses kprobes.

## Test beds

No SSD and no NIC is involved:

- `tests/loopback.sh tcp|rdma up [completion_ns] [queues]` exports a null_blk
  device through the kernel target and connects to it (stock modules,
  configfs). `tcp` runs over 127.0.0.1; `rdma` over soft-RoCE (`rdma_rxe`)
  bound to the default-route interface. `completion_ns` adds a fixed device
  latency. `tests/loopback.sh tcp|rdma down` removes everything.
- `tests/spdk-target.sh up [delay_us]` starts an SPDK `nvmf_tgt` exporting a
  null bdev on 127.0.0.1:4421 (reserving hugepages if needed); `delay_us` puts
  a delay bdev in front. `tests/spdk-target.sh trid` prints the transport ID
  for `spdk_nvme_perf -r`; `down` stops it.

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
| `src/loader.c`, `src/spdk.c` | BTF choice, hook selection, attach, ring buffer; SPDK processes |
| `src/record.c`, `src/offline.c` | `record`; `report`, `compare`, `dump` |
| `src/model.c` | the stage model |
| `src/join.c`, `src/stats.c`, `src/report.c` | join, aggregation, output |
| `tests/` | unit tests, test beds |
| `tools/gen-btf.sh` | BTF from module DWARF |
