sudo modprobe nvmet-tcp
sudo apt install nvme-cli
sudo nvme connect -t tcp -n mysubsystem -a 128.105.146.93 -s 4420
