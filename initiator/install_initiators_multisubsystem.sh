cd /usr/src/linux-source-5.15.0
sudo make modules SUBDIRS=drivers/nvme/host -j31
sudo rmmod nvme_tcp
sudo rmmod nvme-fabrics
sudo modprobe nvme-core
sudo insmod ./drivers/nvme/host/nvme-fabrics.ko
sudo insmod ./drivers/nvme/host/nvme-tcp.ko
sudo apt install nvme-cli
# sudo nvme connect -t tcp -n mysubsystem -a 128.105.146.92 -s 4420 -l 128.105.146.85
sudo nvme connect -t tcp -n mysubsystem1 -a 10.10.1.2 -s 4421 -l 10.10.1.1
sudo nvme connect -t tcp -n mysubsystem2 -a 10.10.1.2 -s 4422 -l 10.10.1.1
sudo nvme connect -t tcp -n mysubsystem3 -a 10.10.1.2 -s 4423 -l 10.10.1.1
sudo nvme connect -t tcp -n mysubsystem4 -a 10.10.1.2 -s 4424 -l 10.10.1.1