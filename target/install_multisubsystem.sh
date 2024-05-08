cd /usr/src/linux-source-5.15.0
sudo make modules SUBDIRS=drivers/nvme/target -j31
sudo insmod ./drivers/nvme/target/nvmet.ko
sudo insmod ./drivers/nvme/target/nvmet-tcp.ko
cd ~

# Function to create NVMe subsystem, namespace, and port
create_subsystem_namespace_port() {
    local subsystem="$1"
    local namespace_id="$2"
    local device_path="$3"
    local port_id="$4"

    # Create subsystem
    sudo mkdir -p /sys/kernel/config/nvmet/subsystems/"$subsystem"
    sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/$subsystem/attr_allow_any_host"

    # Create namespace
    sudo mkdir /sys/kernel/config/nvmet/subsystems/"$subsystem"/namespaces/"$namespace_id"
    sudo sh -c "echo -n $device_path > /sys/kernel/config/nvmet/subsystems/$subsystem/namespaces/$namespace_id/device_path"
    sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/$subsystem/namespaces/$namespace_id/enable"

    # Create and configure port
    sudo mkdir -p /sys/kernel/config/nvmet/ports/"$port_id"
    sudo sh -c "echo ipv4 > /sys/kernel/config/nvmet/ports/$port_id/addr_adrfam"
    sudo sh -c "echo 10.10.1.2 > /sys/kernel/config/nvmet/ports/$port_id/addr_traddr"
    sudo sh -c "echo tcp > /sys/kernel/config/nvmet/ports/$port_id/addr_trtype"
    sudo sh -c "echo $((4420 + $port_id)) > /sys/kernel/config/nvmet/ports/$port_id/addr_trsvcid"

    # Link subsystem to its port
    sudo ln -s /sys/kernel/config/nvmet/subsystems/"$subsystem" /sys/kernel/config/nvmet/ports/"$port_id"/subsystems/"$subsystem"
}

# Create and configure subsystem, namespace, and port for each device
create_subsystem_namespace_port mysubsystem1 7 /dev/nvme0n1 1
create_subsystem_namespace_port mysubsystem2 8 /dev/nvme1n1 2
create_subsystem_namespace_port mysubsystem3 9 /dev/nvme2n1 3
create_subsystem_namespace_port mysubsystem4 10 /dev/nvme3n1 4
