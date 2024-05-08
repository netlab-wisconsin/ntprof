cd /usr/src/linux-source-5.15.0
sudo make modules SUBDIRS=drivers/nvme/target -j31
sudo insmod ./drivers/nvme/target/nvmet.ko
sudo insmod ./drivers/nvme/target/nvmet-tcp.ko
cd ~

# 创建 NVMe 子系统
sudo mkdir -p /sys/kernel/config/nvmet/subsystems/mysubsystem
sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/mysubsystem/attr_allow_any_host"

# 创建命名空间并为每个 NVMe 设备配置命名空间
create_namespace() {
    local namespace_id="$1"
    local device_path="$2"
    sudo mkdir /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/"$namespace_id"
    sudo sh -c "echo -n $device_path > /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/$namespace_id/device_path"
    sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/$namespace_id/enable"
}

# 为每个设备创建和配置命名空间
create_namespace 7 /dev/nvme0n1
create_namespace 8 /dev/nvme1n1
create_namespace 9 /dev/nvme2n1
create_namespace 10 /dev/nvme3n1

# 创建 NVMe over TCP 端口
sudo mkdir -p /sys/kernel/config/nvmet/ports/1
sudo sh -c "echo ipv4 > /sys/kernel/config/nvmet/ports/1/addr_adrfam"
sudo sh -c "echo 10.10.1.2 > /sys/kernel/config/nvmet/ports/1/addr_traddr"
sudo sh -c "echo tcp > /sys/kernel/config/nvmet/ports/1/addr_trtype"
sudo sh -c "echo 4420 > /sys/kernel/config/nvmet/ports/1/addr_trsvcid"

# 将子系统链接到端口
sudo ln -s /sys/kernel/config/nvmet/subsystems/mysubsystem /sys/kernel/config/nvmet/ports/1/subsystems/mysubsystem


