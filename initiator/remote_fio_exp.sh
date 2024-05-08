#!/bin/bash

# Define the output file
output_file="fio_results.csv"

# Check if the output file already exists
if [ -f "$output_file" ]; then
    # Ask the user if they want to remove the existing file
    read -p "$output_file exists. Remove it? (y/n): " answer

    # If the user answers 'y', remove the file
    if [ "$answer" = "y" ]; then
        rm "$output_file"
        echo "Removed $output_file."
    else
        echo "Continuing without removing $output_file."
    fi
fi

# Define the workload types, I/O depths, and the CSV header
# workload_types=("write" "randwrite")
workload_types=("read" "randread")
io_depths=(1 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32)
echo "Workload Type,I/O Depth,Block Size,Average Latency (usec),IOPS,Bandwidth (MB/s)" > "$output_file"

# Function to extract metrics from fio output
extract_metrics() {
    local workload_type="$1"
    local io_depth="$2"
    local block_size="$3"
    local fio_output="$4"

    # Extract IOPS
    iops=$(echo "$fio_output" | grep -oP 'IOPS=\K[\d.]+k?' | tr -d 'K' | awk '{print ($0+0) * (/k$/ ? 1000 : 1)}')
    
    # Directly extract bandwidth in MB/s
    bw_mbs=$(echo "$fio_output" | grep -oP 'BW=\K\d+MiB/s \((\d+)MB/s\)' | grep -oP '\(\d+MB/s\)' | grep -oP '\d+')

    # Extract and convert average latency
    # First, try to extract latency in microseconds (usec)
    avg_lat_usec=$(echo "$fio_output" | grep -Po ' lat \(usec\):.*avg=\K\d+(\.\d+)?')

    # If not found, try to extract in milliseconds (msec) and convert to usec
    if [ -z "$avg_lat_usec" ]; then
        avg_lat_msec=$(echo "$fio_output" | grep -Po ' lat \(msec\):.*avg=\K\d+(\.\d+)?')
        if [ -n "$avg_lat_msec" ]; then
            # Convert msec to usec
            avg_lat=$(echo "$avg_lat_msec" | awk '{print $1 * 1000}')
        else
            echo "Error: Average latency unit not recognized."
            echo "Raw fio output:"
            echo "$fio_output"
            return 1 # Return with error
        fi
    else
        avg_lat=$avg_lat_usec
    fi

    # Check for extraction errors
    if [ -z "$iops" ] || [ -z "$bw_mbs" ] || [ -z "$avg_lat" ]; then
        echo "Error extracting metrics for workload type=$workload_type, iodepth=$io_depth, block size=$block_size"
        echo "Raw fio output:"
        echo "$fio_output"
        return 1 # Return with error
    fi

    # Save the metrics to the output file
    echo "$workload_type,$io_depth,$block_size,$avg_lat,$iops,$bw_mbs" >> "$output_file"
}

# Main loop to run fio with different parameters
for workload_type in "${workload_types[@]}"; do
    for io_depth in "${io_depths[@]}"; do
        # Set block size based on workload type
        block_size="4K" # Default
        if [ "$workload_type" == "read" ]; then
            block_size="128K"
        fi

        echo "Running fio with workload type=$workload_type, iodepth=$io_depth, and block size=$block_size..."

        # Execute fio command and store output
        fio_output=$(sudo fio --name=test --filename=/dev/nvme4n1 --rw=$workload_type --ioengine=libaio --numjobs=16 --time_based --runtime=20 --group_reporting --bs=$block_size --direct=1 --iodepth=$io_depth)
        
        # Extract and save the required metrics
        if ! extract_metrics "$workload_type" "$io_depth" "$block_size" "$fio_output"; then
            echo "Failed to extract metrics. See error above."
        fi
    done
done

echo "Experiments completed. Results saved to $output_file."
