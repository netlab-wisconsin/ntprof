# Queue depth

**Question:** latency grows when the queue depth goes up. Is the SSD saturating, or something on the way?

**Run:** 4 KiB random reads from one fio job at queue depth 1 and at 32, over NVMe/TCP and NVMe/RDMA
([testbed](README.md)).

```
== read 4KiB nvme5n1: baseline 2540 commands sampled, loaded 2208            (NVMe/TCP)
  stage                                    base us   load us     LAD
  host block layer                            0.52      0.55    1.06
  host driver (nvme-tcp)                      3.77      4.63    1.23
  network (TCP/IP, NICs, wire)               19.83    187.57    9.46
  target driver (nvmet-tcp)                   2.45     16.67    6.80
  storage (target block layer, drive)        59.98     66.78    1.11
  end to end                                 86.56    276.21    3.19
  largest LAD: network (TCP/IP, NICs, wire), 9.46

== read 4KiB nvme5n1: baseline 3081 commands sampled, loaded 5492            (NVMe/RDMA)
  stage                                    base us   load us     LAD
  host block layer                            0.52      0.39    0.75
  host driver (nvme-rdma)                     1.27      1.09    0.86
  network (RDMA, NICs, wire)                 10.03     49.57    4.94
  target driver (nvmet-rdma)                  1.01      1.25    1.24
  storage (target block layer, drive)        60.15     67.92    1.13
  end to end                                 72.97    120.22    1.65
  largest LAD: network (RDMA, NICs, wire), 4.94
```

**Answer:** not the SSD. Over NVMe/TCP the read gets 3.2× slower, and the stage that amplifies it is the network
path (9.5×), followed by the target's driver (6.8×); storage grows only 1.1×. At queue depth 32 the network path is
68 % of the latency: one fio job submits every command on one CPU, so all of them share one NVMe/TCP queue and one
TCP connection, which carried about 96,000 I/Os per second (four jobs: 384,000). Spread the load over more CPUs and
queues; a faster SSD would not help. The NSDI '25 paper found the same shape on its testbed.

Over NVMe/RDMA the same ladder costs much less: 73.0 → 120.2 µs at 232,600 I/Os per second, with the network again the
largest LAD (4.9×, most of it the RDMA data transfer waiting for the target to handle its send completions) and
storage 1.1×.

The `avg QD` column of each report shows where the 32 commands wait: over NVMe/TCP, 26.6 of them were in the kernel
on average. The rest had completed and waited in io_uring's completion queue for the fio thread, which is why fio's
mean latency (334 µs) is above ntprof's end to end (276 µs).
