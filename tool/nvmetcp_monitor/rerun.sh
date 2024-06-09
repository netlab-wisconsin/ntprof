make clean
make
sudo rmmod ntm_kernel
sudo insmod ntm_kernel.ko
sudo ./ntm_user -d nvme4n1

