cd kernel
sudo rmmod k_nttm
make clean
make
sudo insmod k_nttm.ko
cd ../user
make clean
make
sudo ./u_nttm track -dev=nvme0n1 -qid=1	-rate=1000 -nrate=1000
