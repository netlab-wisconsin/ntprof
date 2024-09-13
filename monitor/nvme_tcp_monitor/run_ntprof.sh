cd kernel
sudo rmmod k_ntm
make clean
make
sudo insmod k_ntm.ko
cd ../user
make clean
make
sudo ./u_ntm track -dev=nvme4n2 -rate=1 -nrate=100 -detail=false -type=both -mtu=9000 -rtt=1
