#!/bin/bash

# Check if the number of servers is provided as an argument
if [ -z "$1" ]; then
  echo "Usage: $0 <number_of_servers>"
  exit 1
fi

# Number of servers
num_servers=$1

# Start iperf3 servers on specific CPU cores
for ((i=0; i<num_servers; i++)); do
  core=$((16 + i))
  port=$((5101 + i))
  taskset -c $core iperf3 -s -p $port &
  pids[$i]=$!
done

# Function to kill background processes
cleanup() {
  echo "Stopping iperf3 servers..."
  kill "${pids[@]}"
}

# Trap Ctrl+C (SIGINT) and run the cleanup function
trap cleanup SIGINT

# Wait for background processes to finish
wait
