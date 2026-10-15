# Queue depth

**Question:** latency grows when the queue depth goes up. Is the SSD saturating, or something on the way?

**Run:** 4 KiB random reads from one fio job at queue depth 1 and at 32, over NVMe/TCP and NVMe/RDMA
([testbed](README.md)).

```
== read 4KiB nvme6n1: baseline 2694 commands sampled, loaded 2299            (NVMe/TCP)
  stage                                    base us   load us     LAD
  host block layer                            0.53      0.60    1.14
  host driver (nvme-tcp)                      3.82      4.57    1.20
  network (TCP/IP, NICs, wire)               19.68    186.33    9.47
  target driver (nvmet-tcp)                   2.54     16.13    6.34
  storage (target block layer, drive)        60.08     67.77    1.13
  end to end                                 86.65    275.40    3.18
  largest LAD: network (TCP/IP, NICs, wire), 9.47

== read 4KiB nvme6n1: baseline 3066 commands sampled, loaded 5901            (NVMe/RDMA)
  stage                                    base us   load us     LAD
  host block layer                            0.49      0.39     n/a
  host driver (nvme-rdma)                     1.26      1.08    0.86
  network (RDMA, NICs, wire)                 10.03     41.38    4.13
  target driver (nvmet-rdma)                  1.02      1.25    1.22
  storage (target block layer, drive)        59.97     68.32    1.14
  end to end                                 72.77    112.42    1.54
  largest LAD: network (RDMA, NICs, wire), 4.13
```

**Answer:** not the SSD. Over NVMe/TCP the read gets 3.2× slower, and the stage that amplifies it is the network
path (9.5×), followed by the target's driver (6.3×); storage grows only 1.1×. At queue depth 32 the network path is
68 % of the latency: one fio job submits every command on one CPU, so all of them share one NVMe/TCP queue and one
TCP connection, which carried about 94,400 I/Os per second (four jobs: 331,100). Spread the load over more CPUs and
queues; a faster SSD would not help. The NSDI '25 paper found the same shape on its testbed.

Over NVMe/RDMA the same ladder costs much less: 72.8 → 112.4 µs at 255,600 I/Os per second, with the network again
the largest LAD (4.1×, most of it the RDMA data transfer waiting for the target to handle its send completions) and
storage 1.1×.

The `avg QD` column of each report shows where the 32 commands wait: over NVMe/TCP, 27.5 of them were in the kernel
on average. The rest had completed and waited in io_uring's completion queue for the fio thread, which is why fio's
mean latency (339 µs) is above ntprof's end to end (275 µs).
