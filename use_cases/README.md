# Use cases

Each use case is one question about a slow NVMe-oF path and the ntprof run that answers it. All four ran on the same
two servers on 2026-10-15:

| | |
|---|---|
| Servers | two, each with 2 × Intel Xeon Gold 6526Y (64 CPUs), one as host, one as target |
| Network | NVIDIA ConnectX-7, 400 GbE RoCEv2, MTU 9000 |
| Software | Ubuntu 22.04, Linux 5.15, MLNX OFED 24.10's `nvme-tcp`, `nvme-rdma`, `nvmet-tcp`, `nvmet-rdma` |
| Target | one Samsung PM9A3 (1.92 TB), exported through the kernel NVMe target (block back end) |
| Workload | fio 3.38, io_uring, direct I/O, on the host's NUMA node; reads on a region of the drive written sequentially first, writes on another region |
| ntprof | both sides recorded separately and joined: `-s 100` (`-s 1000` at queue depth 32) |

| Question | What ntprof showed |
|---|---|
| [Is the SSD the bottleneck?](busy-drive.md) | A second writer on the drive: storage grows 2.7–2.8×, every other stage stays within 6 %. |
| [Is the network the bottleneck?](busy-network.md) | A bulk flow on the link: over NVMe/RDMA the network grows 1.9×, storage does not move; over NVMe/TCP the effect varied between runs, from none to 2×. |
| [Why does latency grow with queue depth?](queue-depth.md) | NVMe/TCP at queue depth 32: the network path grows 9.5×, storage 1.1×. |
| [Network or storage, per request?](network-vs-storage.md) | NVMe/RDMA: each request's data transfer and drive time from one run. |
