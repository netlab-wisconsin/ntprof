#!/bin/bash
set -e

NAME="[run_exp_host]"
CONFIG_FILE="ntprof_config.ini"
MODULE_NAME="ntprof_host"
HOST_DIR_NAME="host"
CLI_DIR_NAME="cli"
MODULE_PATH="$HOST_DIR_NAME/$MODULE_NAME.ko"
CLI_PATH="$CLI_DIR_NAME/ntprof_cli"
DURATION=10

# check if ini config file is specified
if [ $# -gt 0 ]; then
    CONFIG_FILE="$1"
fi

echo "$NAME Cleaning up old build..."
make clean && make


cleanup() {
    echo "$NAME Stopping experiment and cleaning up..."
    sudo $CLI_PATH stop || true
    if lsmod | grep -q "$MODULE_NAME"; then
        sudo rmmod $MODULE_NAME || true
    fi
    make clean || true
}

trap cleanup EXIT

# Lead the kernel modules
echo "$NAME Loading the module..."
if ! sudo insmod "$MODULE_PATH"; then
    echo "Error: Failed to load kernel module!"
    exit 1
fi

# start collecting data
#echo "$NAME Starting the experiment with config: $CONFIG_FILE"
#if ! sudo $CLI_PATH start "$CONFIG_FILE"; then
#    echo "Error: Experiment failed to start!"
#    exit 1
#fi

# wait for the experiment to finish
echo "$NAME Running for $DURATION seconds..."
# sleep $DURATION

# if ! sudo $CLI_PATH analyze; then
#     echo "Error: Experiment failed to analyze!"
#     exit 1
# fi

# sleep $DURATION
echo "$NAME Experiment completed successfully!"
