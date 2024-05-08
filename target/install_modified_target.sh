cd /usr/src/linux-source-5.15.0
sudo make modules SUBDIRS=drivers/nvme/target -j31
sudo insmod ./drivers/nvme/target/nvmet.ko
sudo insmod ./drivers/nvme/target/nvmet-tcp.ko
cd ~
sudo mkdir /sys/kernel/config/nvmet/subsystems/mysubsystem
sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/mysubsystem/attr_allow_any_host"
sudo mkdir /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/7
sudo sh -c "echo -n /dev/nvme0n1 > /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/7/device_path"
sudo sh -c "echo 1 > /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/7/enable"
sudo mkdir /sys/kernel/config/nvmet/ports/1
sudo sh -c "echo ipv4 > /sys/kernel/config/nvmet/ports/1/addr_adrfam"
sudo sh -c "echo 10.10.1.2 > /sys/kernel/config/nvmet/ports/1/addr_traddr"
sudo sh -c "echo tcp > /sys/kernel/config/nvmet/ports/1/addr_trtype"
sudo sh -c "echo 4420 > /sys/kernel/config/nvmet/ports/1/addr_trsvcid"
sudo ln -s /sys/kernel/config/nvmet/subsystems/mysubsystem /sys/kernel/config/nvmet/ports/1/subsystems/mysubsystem

