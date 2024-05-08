#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
# set -e

# for a brand new system
# for installing a new kernel and make some backup

# update the library
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt upgrade -yq
sudo apt install -y lrzsz

# Check if linux-source-5.15.0 is already installed
if dpkg -l | grep -qw linux-source-5.15.0; then
    echo "linux-source-5.15.0 is installed, removing..."
    sudo apt remove -y linux-source-5.15.0
    sudo rm -fr /usr/src/linux-source-5.15.0
else
    echo "linux-source-5.15.0 is not installed, no need to remove."
fi

# Install linux-source-5.15.0
sudo apt install -y linux-source-5.15.0

# Extract the linux source code and backup some files
cd /usr/src || { echo "Failed to change directory to /usr/src"; exit 1; }
sudo tar -xvf linux-source-5.15.0.tar.bz2 || { echo "Failed to extract linux-source-5.15.0.tar.bz2"; exit 1; }
cd linux-source-5.15.0 || { echo "Failed to change directory to linux-source-5.15.0"; exit 1; }

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
    if ! sudo cp "/usr/src/linux-source-5.15.0/$file" "/usr/src/linux-source-5.15.0/$file.bak"; then
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
