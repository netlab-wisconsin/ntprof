#!/bin/bash
set -e

# This script automates the process of building, loading, and running a kernel module, ntprof_target.
# It performs the following steps:
# 1. Cleans up any previous build artifacts.
# 2. Compiles the target kernel module.
# 3. Loads the compiled kernel module into the Linux kernel.
# 4. Runs the experiment for a specified duration (default: 120 seconds, or user-specified).
# 5. Unloads the kernel module and cleans up after the experiment.
#
# Usage:
#   ./script.sh [duration]
#   - If a duration (in seconds) is provided as an argument, it will use that value.
#   - If no argument is given, it defaults to 120 seconds.
#
# The script ensures cleanup upon termination using a trap to remove the module and clean build files.

NAME="[run_target]"
DIR_NAME="target"
MODULE_NAME="ntprof_target"
MODULE_PATH="$DIR_NAME/$MODULE_NAME.ko"
DURATION=${1:-120}

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

# Load the kernel module
echo "$NAME Loading the module..."
if ! sudo insmod "$MODULE_PATH"; then
    echo "Error: Failed to load kernel module!"
    exit 1
fi

# Wait
echo "$NAME Running for $DURATION seconds..."
sleep "$DURATION"