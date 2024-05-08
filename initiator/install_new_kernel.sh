#!/bin/bash

# Update package lists
echo "Updating package lists..."
sudo apt-get update || { echo "Failed to update package lists, please check your network connection"; exit 1; }

# Install required packages for kernel compilation
echo "Installing required packages..."
sudo apt-get install -y flex bison build-essential libncurses-dev libssl-dev libelf-dev bc dwarves || { echo "Failed to install packages"; exit 1; }

# Start compiling the kernel
echo "Starting kernel compilation..."
cd /usr/src/linux-source-5.15.0/ || { echo "Cannot switch to kernel source directory"; exit 1; }
sudo make -j31 || { echo "Kernel compilation failed"; exit 1; }
sudo make modules_install || { echo "Module installation failed"; exit 1; }
sudo make install || { echo "Kernel installation failed"; exit 1; }

# Update GRUB configuration
echo "Updating GRUB configuration..."
sudo update-grub || { echo "GRUB update failed"; exit 1; }

# Reboot immediately
echo "Kernel installation completed, rebooting now..."
sudo reboot
