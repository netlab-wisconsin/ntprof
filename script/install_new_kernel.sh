#!/bin/bash

# Update package lists
echo "Updating package lists..."
sudo apt-get update || { echo "Failed to update package lists, please check your network connection"; exit 1; }

# Install required packages for kernel compilation
echo "Installing required packages..."
sudo apt-get install -y flex bison build-essential libncurses-dev libssl-dev libelf-dev bc dwarves cpufrequtils || { echo "Failed to install packages"; exit 1; }

# Start compiling the kernel
echo "Starting kernel compilation..."
cd /usr/src/linux-source-5.15.0/ || { echo "Cannot switch to kernel source directory"; exit 1; }
sudo make -j63 || { echo "Kernel compilation failed"; exit 1; }
sudo make modules_install || { echo "Module installation failed"; exit 1; }
sudo make install || { echo "Kernel installation failed"; exit 1; }

# Update GRUB configuration
echo "Updating GRUB configuration..."
sudo update-grub || { echo "GRUB update failed"; exit 1; }


# Apply configuration changes for journald and GRUB
# Step 1: Check and modify /etc/systemd/journald.conf to set Storage to none
JOURNALD_CONF="/etc/systemd/journald.conf"
STORAGE_SETTING=$(grep "^Storage=" $JOURNALD_CONF)

if [ "$STORAGE_SETTING" != "Storage=none" ]; then
    echo "Modifying $JOURNALD_CONF to set Storage to none"
    sudo sed -i 's/^#Storage=.*/Storage=none/' $JOURNALD_CONF
    sudo sed -i 's/^Storage=.*/Storage=none/' $JOURNALD_CONF
    sudo systemctl restart systemd-journald
else
    echo "Storage is already set to none in $JOURNALD_CONF"
fi

# Step 2: Modify /etc/default/grub to set log_buf_len=60G
GRUB_CONF="/etc/default/grub"
GRUB_CMDLINE=$(grep "^GRUB_CMDLINE_LINUX=" $GRUB_CONF)

if [[ "$GRUB_CMDLINE" != *"log_buf_len=60G"* ]]; then
    echo "Modifying $GRUB_CONF to set log_buf_len=60G"
    sudo sed -i 's/^GRUB_CMDLINE_LINUX="\(.*\)"/GRUB_CMDLINE_LINUX="\1 log_buf_len=60G"/' $GRUB_CONF
    sudo update-grub
else
    echo "log_buf_len=60G is already set in $GRUB_CONF"
fi

echo "Configuration changes applied. Please reboot the system for changes to take effect."

# Reboot immediately
echo "Kernel installation completed, rebooting now..."
sudo reboot
