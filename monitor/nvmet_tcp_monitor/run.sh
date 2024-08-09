cd kernel
sudo rmmod k_nttm
sudo insmod k_nttm.ko
cd ../user
command="sudo ./u_nttm track -dev=nvme0n1 -qid=1-31	-rate=1000 -nrate=100 -detail=false -type=both -mtu=9000 -rtt=1"
nohup $command > /tmp/u_ntm.log 2>&1 &
