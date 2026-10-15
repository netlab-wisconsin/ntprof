# The NSDI '25 artifact: ntprof with kernel modules

This directory is ntprof as published with the NSDI'25 paper: two kernel modules (`ntprof_host` on the
initiator, `ntprof_target` on the target) and a user-space utility (`ntprof_cli`). It needs Linux 5.15 with
the tracepoints of [`../kernel_patches`](../kernel_patches/README.md) compiled in, so the kernel must be
patched, rebuilt and rebooted. The use cases in [`use_cases`](use_cases) were run with it.

For new work, use ntprof in [`../ebpf`](../ebpf/README.md): it measures the same stages with eBPF on an
unmodified kernel (no patch, no reboot, no module), adds NVMe/RDMA and SPDK, and reads this version's
`ntprof_config.ini`.

## 1. Patch, build and boot the kernel (both machines)

ntprof v1 relies on tracepoints that the stock `nvme-tcp` and `nvmet-tcp` modules of Linux 5.15 do not have.

```bash
sudo apt-get update
sudo apt-get install -y linux-source-5.15.0
cd /usr/src
sudo tar -xvf linux-source-5.15.0.tar.bz2
```

The source is extracted to `/usr/src/linux-source-5.15.0`. Apply the changes described in
[`../kernel_patches/README.md`](../kernel_patches/README.md), then build, install and reboot:

```bash
sudo apt-get install -y flex bison build-essential libncurses-dev libssl-dev libelf-dev bc dwarves
cd /usr/src/linux-source-5.15.0
sudo make
sudo make modules_install
sudo make install
sudo reboot
```

## 2. Build ntprof v1

```bash
git clone https://github.com/netlab-wisconsin/ntprof.git
cd ntprof/src
sudo apt-get install -y libiniparser-dev
make host cli      # on the initiator: host/ntprof_host.ko and cli/ntprof_cli
make target        # on the target: target/ntprof_target.ko
```

`ntprof_config.ini` is the task specification: which device, I/O type and sizes to profile, online or offline,
the report interval, the sampling frequency and what to report.

The commands below run in `ntprof/src`.

## 3. Online profiling

Set `IS_ONLINE=true` in `ntprof_config.ini`.

1. On the target: `sudo insmod target/ntprof_target.ko`
2. On the initiator: `sudo insmod host/ntprof_host.ko`
3. On the initiator, start profiling and print the reports:
   ```bash
   sudo cli/ntprof_cli start ntprof_config.ini & sudo cli/ntprof_cli analyze ntprof_config.ini
   ```
4. Stop with Ctrl-C. The CLI tells the modules to stop profiling.
5. Unload the modules: `sudo rmmod ntprof_host` on the initiator, `sudo rmmod ntprof_target` on the target.

`./run_target.sh <seconds>` on the target and `./run_host_online.sh <config file>` on the initiator do the
same; stop the latter with Ctrl-C.

## 4. Offline profiling

Set `IS_ONLINE=false` and `DATA_DIR` (default `/tmp/ntprof_data`; it must exist) in `ntprof_config.ini`.

1. Load the modules as above.
2. On the initiator: `sudo cli/ntprof_cli start ntprof_config.ini`
3. On the initiator, when done: `sudo cli/ntprof_cli stop`. The data is saved in `DATA_DIR`.
4. On the initiator: `sudo cli/ntprof_cli analyze ntprof_config.ini` prints the reports.
5. Unload the modules.

`./run_target.sh <seconds>` on the target and `./run_host_offline.sh <config file> <seconds>` on the initiator
do the same.
