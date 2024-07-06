#!/bin/bash

# Update package lists
echo "Updating package lists..."
sudo apt-get update || { echo "Failed to update package lists, please check your network connection"; exit 1; }

# Install required packages for kernel compilation
echo "Installing required packages..."
sudo apt-get install -y flex bison build-essential libncurses-dev libssl-dev libelf-dev bc dwarves cpufrequtils msr-tools || { echo "Failed to install packages"; exit 1; }

# Start compiling the kernel
echo "Starting kernel compilation..."
cd /usr/src/linux-source-5.15.0/ || { echo "Cannot switch to kernel source directory"; exit 1; }
sudo make -j63 || { echo "Kernel compilation failed"; exit 1; }
sudo make modules_install || { echo "Module installation failed"; exit 1; }
sudo make install || { echo "Kernel installation failed"; exit 1; }

# Update GRUB configuration
echo "Updating GRUB configuration..."



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

# Step 3: Modify /etc/default/grub to set intel_pt.disable=1 and intel_pstate=active

GRUB_CMDLINE_DEFAULT=$(grep "^GRUB_CMDLINE_LINUX_DEFAULT=" $GRUB_CONF)

if [[ "$GRUB_CMDLINE_DEFAULT" != *"intel_pt.disable=1"* || "$GRUB_CMDLINE_DEFAULT" != *"intel_pstate=active"* ]]; then
    echo "Modifying $GRUB_CONF to set intel_pt.disable=1 and intel_pstate=active"
    sudo sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/GRUB_CMDLINE_LINUX_DEFAULT="\1 quiet splash intel_pt.disable=1 intel_pstate=active"/' $GRUB_CONF
    sudo update-grub
else
    echo "GRUB_CMDLINE_LINUX_DEFAULT already contains the necessary parameters"
fi

echo "Configuration changes applied. Please reboot the system for changes to take effect."


sudo update-grub || { echo "GRUB update failed"; exit 1; }

# Step 4, install perf and cpupower if not installed

# install perf, if /usr/local/bin/perf exists, then skip the installation
if [ ! -f /usr/local/bin/perf ]; then
    echo "Installing perf..."
    cd /usr/src/linux-source-5.15.0/tools/perf
    sudo make -j31
    sudo cp perf /usr/local/bin
else
    echo "perf is already installed"
fi

# install cpupower, if /usr/bin/cpupower exists, then skip the installation
if [ ! -f /usr/bin/cpupower ]; then
    echo "Installing cpupower..."
    sudo apt-get install libpci-dev gettext
    cd /usr/src/linux-source-5.15.0/tools/power/cpupower
    sudo make
    sudo ln -s libcpupower.so.0 /usr/lib/libcpupower.so.0
    echo "/usr/src/linux-source-5.15.0/tools/power/cpupower" | sudo tee /etc/ld.so.conf.d/cpupower.conf
    sudo ldconfig
    sudo make install
else
    echo "cpupower is already installed"
fi


# setup set_msr service on startup
# it is responsible for (1) disable intel_pt, (2) disable turbo boost, (3) set 'performance' governor
SERVICE_FILE="/etc/systemd/system/set_msr.service"

if [ ! -f "$SERVICE_FILE" ]; then
    # create file if doesn;
    echo "[Unit]
Description=Set MSR Registers at Boot
After=network.target

[Service]
Type=oneshot
ExecStart=/sbin/modprobe msr
ExecStart=/usr/sbin/wrmsr 0x570 0
ExecStart=/usr/sbin/wrmsr -a 0x1a0 0x4000850089
ExecStart=/usr/bin/cpupower frequency-set -g performance
RemainAfterExit=true

[Install]
WantedBy=multi-user.target" | sudo tee $SERVICE_FILE > /dev/null

    # restart systemd settints
    sudo systemctl daemon-reload

    # start set_msr service
    sudo systemctl start set_msr.service
fi

# check the status of set_msr
sudo systemctl status set_msr.service



# Reboot immediately
echo "Kernel installation completed, rebooting now..."
sudo reboot
