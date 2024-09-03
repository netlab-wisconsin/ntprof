#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
# set -e

# for a brand new system
# for installing a new kernel and make some backup

# Specify the kernel version you want to use
KERNEL_VERSION="5.15.143"

# update the library
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt upgrade -yq
sudo apt install -y lrzsz

# Check if linux-5.15.143 is already installed
if dpkg -l | grep -qw linux-$KERNEL_VERSION; then
    echo "linux-$KERNEL_VERSION is installed, removing..."
    sudo apt remove -y linux-$KERNEL_VERSION
    sudo rm -fr /usr/src/linux-$KERNEL_VERSION
else
    echo "linux-$KERNEL_VERSION is not installed, no need to remove."
fi



# Download and extract the specific kernel version from kernel.org
cd /usr/src || { echo "Failed to change directory to /usr/src"; exit 1; }
sudo wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-$KERNEL_VERSION.tar.xz || { echo "Failed to download linux-$KERNEL_VERSION.tar.xz"; exit 1; }
sudo tar -xvf linux-$KERNEL_VERSION.tar.xz || { echo "Failed to extract linux-$KERNEL_VERSION.tar.xz"; exit 1; }
cd linux-$KERNEL_VERSION || { echo "Failed to change directory to linux-$KERNEL_VERSION"; exit 1; }

# The config file might vary by installed kernel versions; finding the latest
latest_config=$(ls -v /boot/config-* | tail -n 1)
if [ -z "$latest_config" ]; then
    echo "Failed to find the latest kernel config in /boot"
    exit 1
fi
sudo cp "$latest_config" .config

# Backup various tcp-related files
files_to_backup=(
    "include/net/tcp.h"
    "net/ipv4/tcp.c"
    "net/ipv4/tcp_input.c"
    "net/ipv4/tcp_output.c"
    "net/ipv4/tcp_cong.c"
    "drivers/nvme/host/tcp.c"
    "drivers/nvme/target/tcp.c"
    "drivers/nvme/target/io-cmd-bdev.c"
)

for file in "${files_to_backup[@]}"; do
    if ! sudo cp "/usr/src/linux-$KERNEL_VERSION/$file" "/usr/src/linux-$KERNEL_VERSION/$file.bak"; then
        echo "Failed to backup $file"
        exit 1
    fi
done

# Assuming ./install_new_kernel.sh is properly permissioned and in the correct path
cd ~ || exit 1
if ! ./install_new_kernel.sh; then
    echo "Error occurred, stopping before running ./install_new_kernel.sh"
    exit 1
fi
