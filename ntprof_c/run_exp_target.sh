#!/bin/bash
set -e

NAME="[run_exp_target]"
MODULE_NAME="ntprof_target"
MODULE_PATH="host/$MODULE_NAME.ko"
DURATION=10

echo "$NAME Cleaning up old build..."
make clean && make target


cleanup() {
    echo "$NAME Stopping experiment and cleaning up..."
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

# wait for the experiment to finish
echo "$NAME Running for $DURATION seconds..."
sleep $DURATION

echo "$NAME Experiment completed successfully!"
