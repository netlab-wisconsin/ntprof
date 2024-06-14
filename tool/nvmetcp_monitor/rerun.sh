sudo rmmod k_ntm
make clean
make
sudo insmod k_ntm.ko
sudo ./u_ntm track -dev=nvme4n1 -type=write

