# A busy drive

**Question:** a remote read got slower. Is the target's SSD the reason?

**Run:** 4 KiB random reads from the host at queue depth 1, first alone, then while a local fio job on the target
writes 128 KiB blocks to another region of the same SSD (random writes, queue depth 8). Both sides recorded, over
NVMe/RDMA and NVMe/TCP ([testbed](README.md)).

```bash
# on the target, beside the recording: the interfering writer
sudo fio --name=busy --filename=/dev/nvme0n1 --rw=randwrite --bs=128k --iodepth=8 \
    --ioengine=io_uring --direct=1 --offset=128G --size=64G --time_based --runtime=60
# later, anywhere
ntprof compare base/host.ntp,base/target.ntp busy/host.ntp,busy/target.ntp
```

```
== read 4KiB nvme5n1: baseline 3008 commands sampled, loaded 1379
  stage                                    base us   load us     LAD
  host block layer                            0.51      0.50    0.98
  host driver (nvme-rdma)                     1.26      1.27    1.01
  network (RDMA, NICs, wire)                 10.03     10.09    1.01
  target driver (nvmet-rdma)                  1.01      1.07    1.06
  storage (target block layer, drive)        60.02    162.06    2.70
  end to end                                 72.82    174.98    2.40
  largest LAD: storage (target block layer, drive), 2.70
```

**Answer:** yes. The read became 2.4× slower (72.8 → 175.0 µs), and all of it is in storage (LAD 2.70); every
other stage stays within 6 %. `--detail` puts it in the drive itself (59.3 → 161.5 µs from `block_rq_issue` to
`block_rq_complete`), not in the target's block layer (0.5–0.6 µs either way). Over NVMe/TCP the result is the same:
storage 60.1 → 162.3 µs (LAD 2.70), the other stages 1.00–1.01.

This is the case the NSDI '25 paper studies in its §4.4, where a busy SSD made storage 5.7× slower.
