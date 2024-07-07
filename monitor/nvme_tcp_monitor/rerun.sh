cd kernel
sudo rmmod k_ntm
make clean
make
sudo insmod k_ntm.ko
cd ../user
make clean
make
sudo ./u_ntm track -dev=nvme4n1 -rate=1000 -nrate=100

