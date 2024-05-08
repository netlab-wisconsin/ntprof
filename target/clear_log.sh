sudo nvme disconnect -n mysubsystem
sudo dmesg --clear
sudo journalctl --rotate
sudo journalctl --vacuum-time=1s
sudo truncate -s 0 /var/log/syslog
sudo truncate -s 0 /var/log/kern.log
sudo truncate -s 0 /var/log/kern.log.1
sudo truncate -s 0 /var/log/syslog.1
