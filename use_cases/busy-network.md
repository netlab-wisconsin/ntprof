# A busy network

**Question:** a remote read got slower while other traffic shares the link. Is the network the reason?

**Run:** 4 KiB random reads at queue depth 1, first alone, then beside a bulk flow from the target to the host, the
direction of the read data ([testbed](README.md)):

- NVMe/RDMA: `ib_write_bw` with 4 queue pairs and 1 MiB messages, 391.8 Gb/s (the link saturated);
- NVMe/TCP: iperf3 with 8 streams, 24.2 Gb/s (one iperf3 process).

```
== read 4KiB nvme5n1: baseline 3051 commands sampled, loaded 2648            (NVMe/RDMA)
  stage                                    base us   load us     LAD
  host block layer                            0.49      0.49     n/a
  host driver (nvme-rdma)                     1.27      1.24    0.97
  network (RDMA, NICs, wire)                 10.01     18.91    1.89
  target driver (nvmet-rdma)                  1.00      0.99    0.99
  storage (target block layer, drive)        60.06     60.02    1.00
  end to end                                 72.83     81.65    1.12
  largest LAD: network (RDMA, NICs, wire), 1.89

== read 4KiB nvme5n1: baseline 2562 commands sampled, loaded 2082            (NVMe/TCP)
  stage                                    base us   load us     LAD
  host block layer                            0.54      0.55    1.02
  host driver (nvme-tcp)                      3.77      4.20    1.11
  network (TCP/IP, NICs, wire)               19.42     38.82    2.00
  target driver (nvmet-tcp)                   2.44      2.75    1.13
  storage (target block layer, drive)        60.14     60.09    1.00
  end to end                                 86.30    106.40    1.23
  largest LAD: network (TCP/IP, NICs, wire), 2.00
```

(`n/a`: a stage under 0.5 µs at the baseline is too small for a ratio.)

**Answer:** yes, and only the network. Over NVMe/RDMA the read's data transfer (its RDMA WRITE and response, from
posting to the send completion, in `--detail`) doubled, 7.1 → 15.2 µs, and the network stage grew 1.9×; storage did
not move. Over NVMe/TCP, even a 24 Gb/s flow on a 400 GbE link doubled the network stage, and the p99 of its computed
part ("network: wire and NICs" in `--detail`) rose from 12.5 to 105 µs: the TCP/IP stacks of both machines are shared
with the flow, not only the wire.
