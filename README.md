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
Please refer to the [kernel source modification document](kernel_change/modify_kernel.md) for details.

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

