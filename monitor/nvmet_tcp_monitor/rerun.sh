cd kernel
sudo rmmod k_nttm
make clean
make
sudo insmod k_nttm.ko
cd ../user
make clean
make
sudo ./u_nttm track -dev=nvme0n1 -qid=1-31	-rate=1000 -nrate=100 -detail=false -type=read -batch_thred=10000
