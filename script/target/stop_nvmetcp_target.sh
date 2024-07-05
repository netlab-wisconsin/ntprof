#!/bin/bash

# 解除子系统与端口的链接
sudo unlink /sys/kernel/config/nvmet/ports/1/subsystems/mysubsystem

# 删除端口配置
sudo rmdir /sys/kernel/config/nvmet/ports/1

# 删除命名空间
sudo rmdir /sys/kernel/config/nvmet/subsystems/mysubsystem/namespaces/7

# 删除子系统
sudo rmdir /sys/kernel/config/nvmet/subsystems/mysubsystem

# 卸载内核模块
sudo rmmod nvmet-tcp
sudo rmmod nvmet

echo "NVMe over TCP target service has been stopped."

