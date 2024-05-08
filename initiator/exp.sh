
sudo modprobe nvme-core
sudo modprobe nvme-fabrics
sudo insmod ./drivers/nvme/host/nvme-tcp.ko
sudo apt install nvme-cli
sudo nvme connect -t tcp -n mysubsystem -a 128.105.146.85 -s 4420
# sudo nvme connect -t tcp -n mysubsystem -a 10.10.1.3 -s 4420

sudo fio --name=wt --filename=/dev/nvme4n1 --rw=write --ioengine=libaio --iodepth=32 --numjobs=16 --time_based --runtime=60 --group_reporting --bs=128K --direct=1

sudo nvme disconnect -n mysubsystem

#./clear_log.sh
