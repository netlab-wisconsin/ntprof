# ntprof

# required packets
```bash
# (1) iniparser
sudo apt-get install libiniparser-dev
```

# build and compile modules

The project `ntprof` is composed of 3 parts:
- `ntprof_host` is the kernel module that runs on the nvme-tcp host, which collects io statistics and execute profiling queries.
- `ntprof_target` is the kernel module that runs on the nvme-tcp target, which collects io statistics and execute profiling queries.
- `ntprof_query` is the user-space tool that sends profiling queries along the io execution path.

To compile all submodules, run the following command:
```bash
make all
```

To compile a specific submodule, run the following command:
```bash
make <submodule>
```

For example, to compile the `ntprof_host` module, run the following command:
```bash
make ntprof_host
```

To clean all submodules, run the following command:
```bash
make clean
```
