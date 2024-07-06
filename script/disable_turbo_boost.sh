#!/bin/bash

# Create the systemd service file
SERVICE_FILE="/etc/systemd/system/disable-turbo-boost.service"

sudo bash -c "cat > $SERVICE_FILE" <<EOF
[Unit]
Description=Disable Turbo Boost

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo'
RemainAfterExit=true

[Install]
WantedBy=multi-user.target
EOF

# Reload systemd daemon to recognize the new service
sudo systemctl daemon-reload

# Enable the service to start at boot
sudo systemctl enable disable-turbo-boost.service

# Start the service immediately
sudo systemctl start disable-turbo-boost.service

# Verify the service status
sudo systemctl status disable-turbo-boost.service

