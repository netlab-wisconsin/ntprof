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
== read 4KiB nvme6n1: baseline 3102 commands sampled, loaded 1267
  stage                                    base us   load us     LAD
  host block layer                            0.49      0.49     n/a
  host driver (nvme-rdma)                     1.24      1.28    1.03
  network (RDMA, NICs, wire)                 10.02     10.08    1.01
  target driver (nvmet-rdma)                  1.06      1.09    1.03
  storage (target block layer, drive)        60.10    160.33    2.67
  end to end                                 72.91    173.26    2.38
  largest LAD: storage (target block layer, drive), 2.67
```

(`n/a`: a stage under 0.5 µs at the baseline is too small for a ratio.)

**Answer:** yes. The read became 2.4× slower (72.9 → 173.3 µs), and all of it is in storage (LAD 2.67); every
other stage stays within 3 %. `--detail` puts it in the drive itself (59.5 → 159.7 µs from `block_rq_issue` to
`block_rq_complete`), not in the target's block layer (0.6 µs either way). Over NVMe/TCP the result is the same:
storage 60.1 → 165.0 µs (LAD 2.75), the other stages 1.00–1.06.

This is the case the NSDI '25 paper studies in its §4.4, where a busy SSD made storage 5.7× slower.
