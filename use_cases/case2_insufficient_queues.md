# Use Case 2: Insufficient Queues

This use case demonstrates how `ntprof` can detect when the number of I/O queues is insufficient for the NVMe-over-TCP target.


### 1. Reducing NVMe-over-TCP I/O Queues to 3
In the Linux kernel, the number of I/O queues is hardcoded in the `nvme_tcp_nr_io_queues` function inside the `/usr/src/linux-source-5.15.0/drivers/nvme/host/tcp.c` file. This function calculates the number of I/O queues based on the number of online CPUs and the number of queues specified in the configuration.

To limit the number of queues to **3**, modify the function as follows:

```c title="tcp.c"
  static unsigned int nvme_tcp_nr_io_queues(struct nvme_ctrl *ctrl)
  {
    unsigned int nr_io_queues;

-   nr_io_queues = min(ctrl->opts->nr_io_queues, num_online_cpus());
+   nr_io_queues = min(3, num_online_cpus());
    nr_io_queues += min(ctrl->opts->nr_write_queues, num_online_cpus());
    nr_io_queues += min(ctrl->opts->nr_poll_queues, num_online_cpus());

    return nr_io_queues;
  }
```


Since this modification affects only the nvme-tcp module, you do not need to recompile and reinstall the entire Linux kernel. Instead, you can recompile and reload only the nvme-tcp module following the steps in [Setting Up an NVMe-over-TCP Initiator](hardware#setting-up-an-nvme-over-tcp-initiator).

### 2. Run the fio workload

We start an fio workload on the initiator side to read 4KB random data from the NVMe-over-TCP target for 5 minutes. The workload is configured to use 32 threads, with an I/O depth of 16, injecting all I/Os into the **3 available I/O queues**.

Run the following command on the initiator side:

```shell
sudo fio --name=test --filename=/dev/nvme4n1 --size=20G --direct=1 --time_based --runtime=300 --cpus_allowed=0-31 --ioengine=libaio --group_reporting --rw=randread --numjobs=32 --bs=4K --iodepth=16
```


### 3. Configuring and Running ntprof

We configure ntprof to profile 4KB read I/Os and report both the average latency breakdown and its distribution. Set the following options in the `ntprof_config.ini` file:

```shell title="ntorpf_config.ini"
IO_TYPE                       ="read"
IO_SIZE                       ="4K"
IS_ONLINE                     =true 
TIME_INTERVAL                 =1
FREQUENCY                     =1000
ENABLE_LATENCY_DISTRIBUTION   =true
ENABLE_LATENCY_BREAKDOWN      =true
```

### 4. Running ntprof
a. On the Target Side

Compile and load the `ntprof_target` module:
```shell
cd ntprof/src
make clean && make target
sudo insmod target/ntprof_target.ko
```

b. On the Initiator Side

Compile and install the `ntprof module`:

```shell
cd ntprof/src
make clean && make host && make cli
sudo insmod host/ntprof_host.ko
```

Start `ntprof` on the initiator side:

```shell
sudo ./cli/ntprof_cli
```

To stop profiling, press `Ctrl+C`. The tool outputs results similar to the following:

```
Analyze result:
Report:
cnt: 1, cat_cnt: 1
group: type=read, size=4096, dev=all, sample#=: 412
=== Latency Breakdown (avg with distribution) ===
blk_submission           : avg 0.44 us, 10% 0.28, 50% 0.39, 70% 0.47, 80% 0.52, 90% 0.61, 95% 0.72, 99% 1.00, 99.9% 4.30
blk_completion           : avg 0.43 us, 10% 0.27, 50% 0.38, 70% 0.45, 80% 0.52, 90% 0.67, 95% 0.78, 99% 1.10, 99.9% 1.54
nvme_tcp_submission      : avg 137.10 us, 10% 15.62, 50% 106.15, 70% 185.63, 80% 243.84, 90% 301.54, 95% 343.21, 99% 423.41, 99.9% 462.57
nvme_tcp_completion      : avg 1.58 us, 10% 0.81, 50% 1.09, 70% 1.23, 80% 1.35, 90% 1.56, 95% 1.86, 99% 15.06, 99.9% 55.95
nvmet_tcp_submission     : avg 0.65 us, 10% 0.48, 50% 0.52, 70% 0.55, 80% 0.58, 90% 0.66, 95% 1.50, 99% 2.37, 99.9% 10.50
nvmet_tcp_completion     : avg 15.01 us, 10% 1.88, 50% 9.16, 70% 17.56, 80% 25.69, 90% 36.18, 95% 48.93, 99% 61.92, 99.9% 128.57
target_subsystem         : avg 224.29 us, 10% 68.72, 50% 166.98, 70% 276.70, 80% 345.72, 90% 451.62, 95% 529.80, 99% 766.39, 99.9% 1551.72
nstack_submission        : avg 108.88 us, 10% 29.24, 50% 93.47, 70% 132.90, 80% 169.24, 90% 220.09, 95% 257.03, 99% 298.40, 99.9% 313.43
nstack_completion        : avg 68.42 us, 10% 12.14, 50% 46.89, 70% 82.50, 80% 108.28, 90% 165.16, 95% 191.28, 99% 302.14, 99.9% 341.95
network_transmission     : avg 167.10 us, 10% 55.52, 50% 128.84, 70% 192.09, 80% 245.57, 90% 346.37, 95% 400.43, 99% 547.46, 99.9% 840.43
==================================================

```

The latency breakdown reveals that the execution time spent on the target subsystem is **224.29 µs**, which is significantly higher than the other latencies. Additionally, the time spent on the NVMe/TCP submission path (nvme_tcp_submission) is **137.10 µs**, and the time requests spend in the network stack on the target side is also high at **108.88 µs**. These indicate potential congestion at the NVMe/TCP layer due to insufficient I/O queues.

### 5. Terminate ntprof and Unload the Module

a. On the Initiator Side:

```shell
sudo rmmod ntprof_host
```

b. On the Target Side:

```shell
sudo rmmod ntprof_target
```