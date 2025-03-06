# ntprof

## Introduction

ntprof is a systematic, informative, and lightweight NVMe/TCP profiler for Linux (Ubuntu). It helps users to understand the performance of NVMe/TCP storage systems. 

ntprof introduces new kernel modules at the initiator (ntprof_host) and target side (ntprof_target), which can be easily installed and removed. It also includes a user-space utility (ntprof-cli) to interact with the kernel modules and display the profiling results.

## Preparation

### Get the Linux kernel source code ready

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

## Getting Started

### Get and compile the ntprof source code

Clone the ntprof repository on both the initiator and target sides. 
``` bash
$ git clone https://github.com/netlab-wisconsin/ntprof.git
$ cd ntprof
```

`src/ntprof_config.ini` is the configuration file for ntprof, which defines the profiling specifications. You can modify the configuration file to customize the profiling requirements. `ntprof` uses the iniparser library to parse the configuration file. You can install the iniparser library by running the following command:
``` bash
$ sudo apt-get install libiniparser-dev
```


- On the initiator side, compile the ntprof-host module and cli tool.
  ``` bash
  $ make host && make cli
  ```

  You can find the compiled kernel module in  `host/ntprof_host.ko` and the compiled cli tool in `cli/ntprof-cli`.

- On the target side, compile the ntprof-target module.
  ``` bash
  $ make target
  ```

  The compiled kernel module is `target/ntprof_target.ko`.


### Online profiling
Assume the current dir is navigated to `ntprof/src/`. 

1. **Step 1**: config the `IS_ONLINE` option in `ntprof_config.ini` to `true` for online profiling.

2. **Step 2**: load the kernel modules on the initiator side
    ``` bash
    $ sudo insmod host/ntprof_host.ko
    ```

3. **Step 3**: load the kernel modules on the target side
    ``` bash
    $ sudo insmod target/ntprof_target.ko
    ```

4. **Step 4**: run the cli tool on the initiator side
    ``` bash
    $ sudo cli/ntprof-cli start src/ntprof_config.ini & sudo cli/ntprof-cli analyze src/ntprof_config.ini
    ```
    The profiling results will be displayed in the terminal.

5. **Step 5**: using Ctrl+C to stop the cli tool. It will send STOP signal to the kernel modules and make it stop profiling.

6. **Step 6**: remove the kernel modules on the initiator side
    ``` bash
    $ sudo rmmod ntprof_host
    ```

7. **Step 7**: remove the kernel modules on the target side
    ``` bash
    $ sudo rmmod ntprof_target
    ```


### Offline profiling
Assume the current dir is navigated to `ntprof/src/`. 

1. **Step 1**: config the `IS_ONLINE` option in `ntprof_config.ini` to `false` for offline profiling, and also specify the output directory `DATA_DIR` for the profiling data. 

2. **Step 2**: load the kernel modules on the initiator side
    ``` bash
    $ sudo insmod host/ntprof_host.ko
    ```

3. **Step 3**: load the kernel modules on the target side
    ``` bash
    $ sudo insmod target/ntprof_target.ko
    ```

4. **Step 4**: run the cli tool on the initiator side
    ``` bash
    $ sudo cli/ntprof-cli start src/ntprof_config.ini
    ```
    The profiling results will be displayed in the terminal.

5. **Step 5**: run the cli tool on the initiator side to stop profiling
    ``` bash
    $ sudo cli/ntprof-cli stop
    ```
    The profiling data will be saved in the specified output directory.
    
6. **Step 6**: run the cli tool on the initiator side to analyze the profiling data
    ``` bash
    $ sudo cli/ntprof-cli analyze src/ntprof_config.ini
    ```
    The profiling results will be displayed in the terminal.

7. **Step 7**: remove the kernel modules on the initiator side
    ``` bash
    $ sudo rmmod ntprof_host
    ```

8. **Step 8**: remove the kernel modules on the target side
    ``` bash
    $ sudo rmmod ntprof_target
    ```
