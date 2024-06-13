make clean
make
sudo rmmod k_ntm
sudo insmod k_ntm.ko
sudo ./u_ntm -d nvme4n1

