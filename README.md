# ntprof

## Introduction

ntprof is a systematic, informative, and lightweight NVMe/TCP profiler for Linux (Ubuntu). It helps users to understand the performance of NVMe/TCP storage systems. 

ntprof introduce new kernel modules at the initiator (ntprof-host) and target side (ntprof-target), which can be easily installed and removed. It also include a user-space utility (cli) to interact with the kernel modules and display the profiling results.

## Preparation

### Get the linux kernel source code ready

ntprof relies on Linux kernel (5.15.0) tracepoints to collect I/O statistics. However, the tracepoints provided by the current Linux system for the nvme-tcp module are insufficient to meet the profiling requirements. Therefore, we need to download the Linux kernel source code, add new tracepoints to the source, and then compile and install the customized kernel.

``` bash
$ sudo apt-get update
$ sudo apt-get install -y linux-source-5.15.0
$ cd /usr/src
$ sudo tar -xvf linux-source-5.15.0.tar.bz2
```

The source code will be extracted to `/usr/src/linux-source-5.15.0`.

### Modify the kernel source code

1. Create a new file [`nvme_tcp.h`](kernel_change/include/trace/events/nvme_tcp.h) in the `include/trace/events` directory. This file defines the tracepoints for the nvme-tcp module.

2. Create a new file [`nvmet_tcp.h`](kernel_change/drivers/nvme/host/nvme_tcp.c) in the `include/trace/events` directory. This file defines the tracepoints for the nvmet-tcp module.

3. Modify the `block/blk-core.c` file as folows

4. Modify the `drivers/nvme/host/tcp.c` file as follows

5. Modify the `drivers/nvme/target/tcp.c` file as follows

6. Modify the `include/linux/nvme-tcp.h` file as follows

### Compile and install the kernel, and reboot the system

``` bash
$ sudo apt-get install -y flex, bison, build-essential, libncurses-dev, libssl-dev, libelf-dev, bc, dwarves
$ cd /usr/src/linux-source-5.15.0
$ sudo make
$ sudo make modules_install
$ sudo make install
$ sudo reboot
```

### Getting Started

#### Online profiling


#### Offline profiling

