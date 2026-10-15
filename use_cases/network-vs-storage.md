# Network vs storage, per request

**Question:** for a request served over NVMe/RDMA, how long does moving its data over the network take, and how long
does the drive take? A staged pipeline answers this by timing a NIC stage (an RDMA transfer into host memory) and an
SSD stage separately. ntprof gives both for every sampled request of a real NVMe/RDMA run.

**Run:** 4 KiB and 128 KiB random reads and writes at queue depth 1 over NVMe/RDMA, both sides recorded
([testbed](README.md)), `ntprof report --detail host.ntp target.ntp`. For a 128 KiB write:

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

| Request (queue depth 1) | Data transfer (network) | Storage | Drive | End to end |
|---|---|---|---|---|
| 4 KiB read | 7.1 µs | 60.0 µs | 59.4 µs | 72.8 µs |
| 128 KiB read | 12.3 µs | 93.7 µs | 91.6 µs | 121.2 µs |
| 128 KiB write | 15.8 µs | 39.0 µs | 36.9 µs | 76.8 µs |
| 4 KiB read, link saturated by a 392 Gb/s flow | 15.3 µs | 59.9 µs | 59.3 µs | 81.5 µs |

- **A write's data transfer** is its RDMA READ: the target pulls the data from the host's memory, from posting the
  READ to its completion.
- **A read's data transfer** is its RDMA WRITE and the response, from posting them to the response's send completion.
  It includes the host NIC's acknowledgement, and under load the time until the target handles its send completions.
- **The two move independently:** a busy link doubled the transfer and left the drive alone; a busy drive did the
  opposite ([busy drive](busy-drive.md), [busy network](busy-network.md)).

The storage stage is the drive's own time: the 128 KiB read's 93.7 µs equals the same read measured alone on the
target with fio (93.7 µs), on a region written sequentially first.
