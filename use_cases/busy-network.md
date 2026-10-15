# A busy network

**Question:** a remote read got slower while other traffic shares the link. Is the network the reason?

**Run:** 4 KiB random reads at queue depth 1, first alone, then beside a bulk flow from the target to the host, the
direction of the read data ([testbed](README.md)):

- NVMe/RDMA: `ib_write_bw` with 4 queue pairs and 1 MiB messages, 391.8 Gb/s (the link saturated);
- NVMe/TCP: iperf3 with 8 streams, 24.4 Gb/s (one iperf3 process).

```
== read 4KiB nvme6n1: baseline 3057 commands sampled, loaded 2673            (NVMe/RDMA)
  stage                                    base us   load us     LAD
  host block layer                            0.50      0.48     n/a
  host driver (nvme-rdma)                     1.24      1.20    0.97
  network (RDMA, NICs, wire)                 10.01     18.91    1.89
  target driver (nvmet-rdma)                  1.02      1.02    1.00
  storage (target block layer, drive)        59.98     59.92    1.00
  end to end                                 72.75     81.53    1.12
  largest LAD: network (RDMA, NICs, wire), 1.89
```

(`n/a`: a stage under 0.5 µs at the baseline is too small for a ratio.)

**Answer over NVMe/RDMA:** yes, and only the network. The read's data transfer (its RDMA WRITE and response, from
posting to the send completion, in `--detail`) doubled, 7.1 → 15.3 µs, and the network stage grew 1.89×; storage did
not move.

```
== read 4KiB nvme6n1: baseline 2647 commands sampled, loaded 2586            (NVMe/TCP)
  stage                                    base us   load us     LAD
  host block layer                            0.52      0.53    1.02
  host driver (nvme-tcp)                      3.83      4.17    1.09
  network (TCP/IP, NICs, wire)               19.79     20.39    1.03
  target driver (nvmet-tcp)                   2.47      2.62    1.06
  storage (target block layer, drive)        59.94     60.11    1.00
  end to end                                 86.55     87.83    1.01
  largest LAD: host driver (nvme-tcp), 1.09
```

**Over NVMe/TCP it depends.** In this run the flow left the read almost unchanged: network 19.79 → 20.39 µs (LAD
1.03), end to end 86.55 → 87.83 µs, and the largest LAD, 1.09, is the host driver. In an earlier run on the same
servers, an iperf3 flow of the same kind (8 streams, 24 Gb/s) doubled the network stage, 19.42 → 38.82 µs. The likely
reason, not proven: a TCP flow slows NVMe/TCP only when their receive processing shares CPUs, and the NIC's receive
hash decides that per connection, so the effect can be anything from none to 2×. In both runs ntprof's report agreed
with fio (here 88.5 → 89.1 µs).
