
# Use Case 1: Congested SSD

### 1. Run the Applicatilon

This use case demonstrates how `ntprof` can detect SSD congestion. We create an additional `fio` workload on the **target side** to induce congestion. The `fio` workload issues random writes to the SSD, using 128 KB block sizes. The I/O depth is set to 8, with a single job running for 300 seconds.

Run the following command on the **target side**:

```shell
sudo fio --name=test --filename=/dev/nvme0n1 --size=20G --direct=1 --time_based --runtime=300 --cpus_allowed=0-31 --ioengine=libaio --group_reporting --rw=randwrite --numjobs=1 --bs=128K --iodepth=8
```

Simultaneously, on the **initiator side**, we run the same fio workload to send I/O requests to the target:

```shell
sudo fio --name=test --filename=/dev/nvme4n1 --size=20G --direct=1 --time_based --runtime=300 --cpus_allowed=0-31 --ioengine=libaio --group_reporting --rw=randwrite --numjobs=1 --bs=128K --iodepth=8
```

### 2. Configuring the Profiling Tool

We configure ntprof to profile 128 KB write I/Os and report both the average latency breakdown and its distribution. Set the following options in the `ntprof_config.ini` file:

```shell title="ntorpf_config.ini"
IO_TYPE                       ="write"
IO_SIZE                       ="128K"
IS_ONLINE                     =true 
TIME_INTERVAL                 =1
FREQUENCY                     =1000
ENABLE_LATENCY_DISTRIBUTION   =true
ENABLE_LATENCY_BREAKDOWN      =true
```

### 3. Running ntprof

a. On the Target Side

Compile and load the `ntprof_target` module:
```shell
cd ntprof/src
make clean && make target
sudo insmod target/ntprof_target.ko
```

b. On the Initiator Side

Compile and install the ntprof module:

```shell
cd ntprof/src
make clean && make host && make cli
sudo insmod host/ntprof_host.ko
```

Start ntprof on the initiator side:

```shell
sudo ./cli/ntprof_cli
```

To stop profiling, press `Ctrl+C`. The tool outputs results similar to the following:

```
Analyze result:
Report:
cnt: 1, cat_cnt: 1
group: type=write, size=131072, dev=all, sample#=: 4
=== Latency Breakdown (avg with distribution) ===
blk_submission           : avg 0.35 us, 10% 0.32, 50% 0.35, 70% 0.39, 80% 0.39, 90% 0.39, 95% 0.39, 99% 0.39, 99.9% 0.39
blk_completion           : avg 0.87 us, 10% 0.62, 50% 0.71, 70% 0.86, 80% 1.05, 90% 1.24, 95% 1.34, 99% 1.42, 99.9% 1.43
nvme_tcp_submission      : avg 24.04 us, 10% 18.41, 50% 25.05, 70% 28.82, 80% 28.84, 90% 28.87, 95% 28.88, 99% 28.89, 99.9% 28.89
nvme_tcp_completion      : avg 0.00 us, 10% 0.00, 50% 0.00, 70% 0.00, 80% 0.00, 90% 0.00, 95% 0.00, 99% 0.00, 99.9% 0.00
nvmet_tcp_submission     : avg 30.45 us, 10% 22.90, 50% 29.74, 70% 36.73, 80% 37.65, 90% 38.57, 95% 39.03, 99% 39.40, 99.9% 39.48
nvmet_tcp_completion     : avg 18.04 us, 10% 16.29, 50% 17.41, 70% 17.89, 80% 19.10, 90% 20.30, 95% 20.91, 99% 21.39, 99.9% 21.50
target_subsystem         : avg 1637.69 us, 10% 1580.72, 50% 1624.87, 70% 1651.16, 80% 1678.04, 90% 1704.91, 95% 1718.35, 99% 1729.10, 99.9% 1731.52
nstack_submission        : avg 72.16 us, 10% 65.22, 50% 73.46, 70% 77.55, 80% 77.80, 90% 78.05, 95% 78.17, 99% 78.27, 99.9% 78.29
nstack_completion        : avg 26.56 us, 10% 22.90, 50% 28.27, 70% 28.83, 80% 28.84, 90% 28.85, 95% 28.85, 99% 28.86, 99.9% 28.86
network_transmission     : avg 0.00 us, 10% 0.00, 50% 0.00, 70% 0.00, 80% 0.00, 90% 0.00, 95% 0.00, 99% 0.00, 99.9% 0.00
==================================================
```

From the output, we can observe that the `target_subsystem` latency is 1637.69 μs, which is significantly higher than the other latency components. This indicates that the storage subsystem on the target is congested.

### 4. Terminate ntprof and Unload the Module

a. On the Initiator Side:

```shell
sudo rmmod ntprof_host
```

b. On the Target Side:

```shell
sudo rmmod ntprof_target
```
