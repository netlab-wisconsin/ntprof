#!/bin/bash

# Change to the kernel source directory
cd /usr/src/linux-source-5.15.0

# Compile the nvme-tcp module
if ! sudo make modules SUBDIRS=drivers/nvme/host -j31; then
    echo "Module compilation failed. Exiting."
#    exit 1
fi

# Unload existing nvme-tcp and nvme-fabrics modules
if ! sudo rmmod nvme_tcp; then
    echo "Failed to unload nvme_tcp module. Exiting."
#    exit 1
fi

if ! sudo rmmod nvme-fabrics; then
    echo "Failed to unload nvme-fabrics module. Exiting."
#    exit 1
fi

# Load the nvme-core module
sudo modprobe nvme-core

# Load the new nvme-fabrics and nvme-tcp modules
sudo insmod ./drivers/nvme/host/nvme-fabrics.ko
sudo insmod ./drivers/nvme/host/nvme-tcp.ko

# Install nvme-cli tool
sudo apt install nvme-cli

# Connect to the NVMe over TCP target
sudo nvme connect -t tcp -n mysubsystem -a 10.10.1.2 -s 4420
