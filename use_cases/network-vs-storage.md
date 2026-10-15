# Network vs storage, per request

**Question:** for a request served over NVMe/RDMA, how long does moving its data over the network take, and how long
does the drive take? A staged pipeline answers this by timing a NIC stage (an RDMA transfer into host memory) and an
SSD stage separately. ntprof gives both for every sampled request of a real NVMe/RDMA run.

**Run:** 4 KiB and 128 KiB random reads and writes at queue depth 1 over NVMe/RDMA, both sides recorded
([testbed](README.md)), `ntprof report --detail host.ntp target.ntp`. For a 128 KiB write:

```
  component                                     n      mean       L       p50       p99   (us)
  host block layer, submission               2188      0.20    0.00      0.20      0.25
  host driver, submission                    2188      2.04    0.02      2.03      2.29
  network: wire and NICs (computed)          2188     25.21    0.24     25.19     25.96
    data transfer (RDMA, at the target)      2188     15.73    0.15     15.72     16.09
  network: target receive path               2188      0.29    0.00      0.27      0.53
  target driver, submission                  2188      8.56    0.08      8.58      8.86
  storage: target block layer and drive      2188     38.75    0.37     38.67     41.71
    target block layer, submission           2188      1.78    0.02      1.95      2.15
    drive                                    2188     36.86    0.35     36.56     39.80
    target block layer, completion           2188      0.11    0.00      0.12      0.13
  target driver, completion                  2188      0.44    0.00      0.44      0.55
  network: host receive path                 2188      0.22    0.00      0.22      0.30
  host driver, completion                    2188      0.00    0.00      0.00      0.00
  host block layer, completion               2188      0.69    0.01      0.71      0.89
    handoff to nvme_complete_rq              2188      0.64    0.01      0.66      0.84
  end to end                                 2188     76.41    0.73     76.21     79.68
```

| Request (queue depth 1) | Data transfer (network) | Storage | Drive alone | End to end |
|---|---|---|---|---|
| 4 KiB read | 7.1 µs | 60.1 µs | 59.4 µs | 72.9 µs |
| 128 KiB read | 12.3 µs | 126.2 µs | 124.3 µs | 153.6 µs |
| 128 KiB write | 15.7 µs | 38.7 µs | 36.9 µs | 76.4 µs |
| 4 KiB read, link saturated by a 392 Gb/s flow | 15.2 µs | 60.0 µs | 59.4 µs | 81.7 µs |

- **A write's data transfer** is its RDMA READ: the target pulls the data from the host's memory, from posting the
  READ to its completion.
- **A read's data transfer** is its RDMA WRITE and the response, from posting them to the response's send completion.
  It includes the host NIC's acknowledgement, and under load the time until the target handles its send completions.
- **The two move independently:** a busy link doubled the transfer and left the drive alone; a busy drive did the
  opposite ([busy drive](busy-drive.md), [busy network](busy-network.md)).

The 128 KiB read's storage time here (126 µs) was measured after the run's random writes; on a freshly written
region the same read took 93.0 µs in storage, equal to the drive measured alone on the target (93.0 µs).
