#!/bin/bash

# 检查是否存在/etc/systemd/system/set_msr.service
SERVICE_FILE="/etc/systemd/system/set_msr.service"

if [ ! -f "$SERVICE_FILE" ]; then
    # 如果文件不存在，则创建
    echo "[Unit]
Description=Set MSR Registers at Boot
After=network.target

[Service]
Type=oneshot
ExecStart=/sbin/modprobe msr
ExecStart=/usr/sbin/wrmsr 0x570 0
ExecStart=/usr/sbin/wrmsr -a 0x1a0 0x4000850089
RemainAfterExit=true

[Install]
WantedBy=multi-user.target" | sudo tee $SERVICE_FILE > /dev/null

    # 重新加载 systemd 配置
    sudo systemctl daemon-reload
    
    # 启动 set_msr 服务
    sudo systemctl start set_msr.service
fi

# 检查 set_msr 服务状态
sudo systemctl status set_msr.service

