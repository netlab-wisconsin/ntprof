cd kernel
sudo rmmod k_nttm
make clean
make
sudo insmod k_nttm.ko
cd ../user
make clean
make
sudo ./u_nttm track 	
