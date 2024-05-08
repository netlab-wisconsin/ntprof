cd /usr/src/linux-source-5.15.0
sudo make modules SUBDIRS=drivers/nvme/host -j31
sudo rmmod nvme_tcp
sudo rmmod nvme-fabrics
sudo modprobe nvme-core
sudo insmod ./drivers/nvme/host/nvme-fabrics.ko
sudo insmod ./drivers/nvme/host/nvme-tcp.ko
sudo apt install nvme-cli
sudo nvme connect -t tcp -n mysubsystem -a 128.105.146.92 -s 4420
# sudo nvme connect -t tcp -n mysubsystem -a 10.10.1.2 -s 4420
