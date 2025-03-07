#!/bin/bash
set -e

# This script automates the process of:
# 1. Building the host kernel module and CLI tool.
# 2. Loading the kernel module.
# 3. Running the profiling experiment with a specified configuration file.
# 4. Collecting and analyzing profiling data.
# 5. Cleaning up resources after execution.

# Usage:
#   ./script.sh [config_file] [duration]
#   - config_file: (optional) Path to the configuration file (default: ntprof_config.ini).
#   - duration: (optional) Experiment duration in seconds (default: 3 seconds).

NAME="[run_exp_host]"
CONFIG_FILE=${1:-"ntprof_config.ini"}  # First argument: config file (default: ntprof_config.ini)
DURATION=${2:-120}  # Second argument: duration in seconds (default: 120)

MODULE_NAME="ntprof_host"
HOST_DIR_NAME="host"
CLI_DIR_NAME="cli"
MODULE_PATH="$HOST_DIR_NAME/$MODULE_NAME.ko"
CLI_PATH="$CLI_DIR_NAME/ntprof_cli"

echo "$NAME Cleaning up old build..."
make clean && make host && make cli

cleanup() {
    echo "$NAME Stopping experiment and cleaning up..."
    sudo $CLI_PATH stop || true
    if lsmod | grep -q "$MODULE_NAME"; then
        sudo rmmod $MODULE_NAME || true
    fi
    make clean || true
}

trap cleanup EXIT

# Load the kernel module
echo "$NAME Loading the module..."
if ! sudo insmod "$MODULE_PATH"; then
    echo "Error: Failed to load kernel module!"
    exit 1
fi

# Start collecting data
echo "$NAME sends START config: $CONFIG_FILE"
if ! sudo $CLI_PATH start "$CONFIG_FILE"; then
    echo "Error: Experiment failed to start!"
    exit 1
fi

# Wait for the experiment to finish
echo "$NAME Waiting for $DURATION seconds to collect profile records..."
sleep "$DURATION"

echo "$NAME cli sends STOP"
if ! sudo $CLI_PATH stop; then
    echo "Error: Experiment failed to stop!"
    exit 1
fi

echo "$NAME cli sends ANALYZE"
if ! sudo $CLI_PATH analyze; then
    echo "Error: Experiment failed to analyze!"
    exit 1
fi
