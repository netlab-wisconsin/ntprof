#!/bin/bash

KERNEL_VERSION="5.15.143"

# update the library
sudo apt-get update
# sudo DEBIAN_FRONTEND=noninteractive apt upgrade -yq
sudo apt install -y lrzsz

# Check if linux-5.15.143 is already installed
if dpkg -l | grep -qw linux-$KERNEL_VERSION; then
    echo "linux-$KERNEL_VERSION is installed, removing..."
    sudo apt remove -y linux-$KERNEL_VERSION
    sudo rm -fr /usr/src/linux-$KERNEL_VERSION
else
    echo "linux-$KERNEL_VERSION is not installed, no need to remove."
fi

cd /usr/src || { echo "Failed to change directory to /usr/src"; exit 1; }
sudo wget https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-$KERNEL_VERSION.tar.xz || { echo "Failed to download linux-$KERNEL_VERSION.tar.xz"; exit 1; }
sudo tar -xvf linux-$KERNEL_VERSION.tar.xz || { echo "Failed to extract linux-$KERNEL_VERSION.tar.xz"; exit 1; }
cd linux-$KERNEL_VERSION || { echo "Failed to change directory to linux-$KERNEL_VERSION"; exit 1; }

latest_config=$(ls -v /boot/config-* | tail -n 1)
if [ -z "$latest_config" ]; then
    echo "Failed to find the latest kernel config in /boot"
    exit 1
fi
sudo cp "$latest_config" .config

# install
sudo apt-get install -y flex bison build-essential libncurses-dev libssl-dev libelf-dev bc dwarves cpufrequtils msr-tools dwarves || { echo "Failed to install packages"; exit 1; }
sudo scripts/config --disable SYSTEM_TRUSTED_KEYS
sudo scripts/config --disable SYSTEM_REVOCATION_KEYS
sudo make -j32 || { echo "Kernel compilation failed"; exit 1; }
sudo make modules_install || { echo "Module installation failed"; exit 1; }
sudo make install || { echo "Kernel installation failed"; exit 1; }