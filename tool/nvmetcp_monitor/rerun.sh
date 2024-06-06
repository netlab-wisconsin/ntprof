make clean
make
sudo rmmod nvmetcp_monitor_kernel
sudo insmod nvmetcp_monitor_kernel.ko
sudo ./nvmetcp_monitor_user -d nvme4n1

