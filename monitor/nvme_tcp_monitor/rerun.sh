cd kernel
sudo rmmod k_ntm
make clean
make
sudo insmod k_ntm.ko
cd ../user
make clean
make
command="sudo ./u_ntm track -dev=nvme4n1 -rate=1000 -nrate=100 -detail=false -type=both -mtu=9000 -rtt=1"
nohup $command > /tmp/u_ntm.log 2>&1 &
pid=$!
echo "Command is running with PID: $pid"

