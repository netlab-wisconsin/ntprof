#!/bin/bash

# Check if the number of clients and total bandwidth are provided as arguments
if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 <number_of_clients> <total_bandwidth_in_Gbps>"
  exit 1
fi

# Number of clients and total bandwidth
num_clients=$1
total_bandwidth=$2

# Calculate average bandwidth per client
average_bandwidth=$(echo "$total_bandwidth / $num_clients" | bc -l)

# Start iperf3 clients on specific CPU cores, targeting the server at 10.10.1.1
for ((i=0; i<num_clients; i++)); do
  core=$((8 + i))
  port=$((5101 + i))
  taskset -c $core iperf3 -c 10.10.1.1 -t 12000 -p $port -b ${average_bandwidth}G &
  pids[$i]=$!
done

# Trap Ctrl+C (SIGINT) and kill background processes
trap "kill ${pids[@]}; exit" SIGINT

# Wait for processes to finish
wait
