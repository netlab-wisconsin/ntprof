#!/bin/bash

# File locations and configurations
output_file="fio_results.csv"
local_dir="$HOME/nvme-tcp/monitor/nvme_tcp_monitor"
remote_dir="$HOME/nvme-tcp/monitor/nvmet_tcp_monitor"
local_script="run.sh"
log_file="/tmp/u_ntm.log"
username="yuyuan"
target_ip="128.105.146.84"

# Prepare the output file
if [ -f "$output_file" ]; then
    rm "$output_file"
fi

# Configuration arrays for the experiments
workload_types=("randread")
io_depths=(1 2 3 4 5 6 7 8 9 10)
jobs=(1)
devices=("/dev/nvme4n1")
block_sizes=("128K")

# Write the CSV header
echo "Workload,io_depth,block_size,jobn,sub_lat(us),mean_lat(usec),p999lat(usec),iops,bw(MB/s),,Local Rerun" > "$output_file"

# Function to start run.sh
run_rerun_script() {
    echo "Starting local run.sh..."
    
    # Ensure the directory and script are valid
    if [ ! -d "$local_dir" ]; then
        echo "Error: Directory $local_dir does not exist."
        exit 1
    fi

    if [ ! -f "$local_dir/$local_script" ]; then
        echo "Error: Script $local_script does not exist in $local_dir."
        exit 1
    fi

    # Ensure the script is executable
    chmod +x "$local_dir/$local_script"

    # Start run.sh and capture the PID
    (cd "$local_dir" && ./"$local_script") &
}

run_remote_rerun_script() {
    echo "Start rmote run.sh..."
    ssh -t -t $1 "cd $remote_dir && sudo ./$local_script" || {
        echo "Error: Failed to start remote script"
        exit 1
    }
}

# Function to stop u_ntm and read its output
stop_rerun_script() {
    local log_content
    sleep 2
    echo "Stopping u_ntm..."
    # Use pkill to terminate the most recent u_ntm
    sudo pkill -n "u_ntm"

    # Wait a moment to ensure the process has time to write to the log
    sleep 2
    # Capture the output from the log file
    log_content=$(tail -n 100 "$log_file")  # Adjust the number of lines as needed
    echo "$log_content"
}

stop_remote_rerun_script() {
    local log_content
    echo "Stopping u_nttm..."
    sleep 2
    ssh -t -t $1 "sudo pkill -n u_nttm" || {
        echo "Error: Failed to stop remote script"
        exit 1
    }
    sleep 2
    log_content=$(ssh -t -t $1 "tail -n 100 $log_file")
    echo "$log_content"
}

# Function to extract latency breakdown from the log content
extract_latency_breakdown() {
    local log_content="$1"
    local workload_type="$2"
    local workload_size="$3"
    

    # make uppercase to lower case
    workload_size=$(echo "$workload_size" | tr '[:upper:]' '[:lower:]')


    # Read the log content line by line
    while IFS= read -r line; do
        # Convert line to lowercase for matching
        line_lower=$(echo "$line" | tr '[:upper:]' '[:lower:]')

        # echo "current line is $line_lower, workload type is $workload_type, workload size is $workload_size"

        # Check if the line contains the workload size
        if [[ "$line_lower" == *"size=${workload_size}"* ]]; then
            # echo "Found size line: $line"
            # Read the next line for 'read' workload type
            if [[ "$workload_type" == *"read"* ]]; then
                read -r next_line
                # split the string with "breakdown" and take the second part
                next_line=$(echo "$next_line" | awk -F 'breakdown' '{print $2}')
                numbers=$(echo "$next_line" | grep -oE '[0-9]+(\.[0-9]+)?')
                result=$(echo "$numbers" | tr '\n' ',' | sed 's/,$//')
                echo "$result"
                break
            # Read the line after the next for 'write' workload type
            elif [[ "$workload_type" == *"write"* ]]; then
                read -r next_line
                read -r next_next_line
                next_next_line=$(echo "$next_next_line" | awk -F 'breakdown' '{print $2}')
                numbers=$(echo "$next_next_line" | grep -oE '[0-9]+(\.[0-9]+)?')
                result=$(echo "$numbers" | tr '\n' ',' | sed 's/,$//')
                echo "$result"
                break
            fi
        fi
    done <<< "$log_content"
}

# Function to run the fio command
run_fio() {
    local device="$1"
    local workload_type="$2"
    local io_depth="$3"
    local block_size="$4"
    local jobn="$5"
    local output_file="${device//\//_}.fio_out"

    echo "Running fio on $device with workload=$workload_type, io_depth=$io_depth, block_size=$block_size, jobn=$jobn"
    sudo fio --name=test --filename="$device" --size=20G --direct=1 --time_based --runtime=10 --cpus_allowed=0-31 --cpus_allowed_policy=split --ioengine=libaio --group_reporting --output-format=terse --rw="$workload_type" --numjobs="$jobn" --bs="$block_size" --iodepth="$io_depth" > "$output_file"
}

# Function to extract and summarize metrics
extract_and_summarize_metrics() {
    local workload_type="$1"
    local io_depth="$2"
    local block_size="$3"
    local jobn="$4"
    local total_iops=0
    local total_bw=0
    local total_mean_lat=0
    local total_p99_9_lat=0
    local count=0
    local total_sub_lat=0

    for device in "${devices[@]}"; do
        output_file_="${device//\//_}.fio_out"
        fio_output=$(cat "$output_file_")

        if [ "$workload_type" == "randread" ]; then
            bw_kib=$(echo "$fio_output" | cut -d';' -f7)
            iops=$(echo "$fio_output" | cut -d';' -f8)
            mean_lat=$(echo "$fio_output" | cut -d';' -f16)
            p99_9_lat=$(echo "$fio_output" | cut -d';' -f32 | cut -d'=' -f2)
            sub_lat=$(echo "$fio_output" | cut -d';' -f12)
        else
            bw_kib=$(echo "$fio_output" | cut -d';' -f48)
            iops=$(echo "$fio_output" | cut -d';' -f49)
            mean_lat=$(echo "$fio_output" | cut -d';' -f57)
            p99_9_lat=$(echo "$fio_output" | cut -d';' -f73 | cut -d'=' -f2)
            sub_lat=$(echo "$fio_output" | cut -d';' -f53)
        fi

        bw_mbs=$(echo "scale=2; $bw_kib / 1024" | bc)
        printf "device=%s, iops=%d, bw=%s MB/s, meanlat=%s usec, p999lat=%s usec\n" "$device" "$iops" "$bw_mbs" "$mean_lat" "$p99_9_lat"
        total_iops=$((total_iops + iops))
        total_bw=$(echo "$total_bw + $bw_mbs" | bc)
        total_mean_lat=$(echo "$total_mean_lat + $mean_lat" | bc)
        total_p99_9_lat=$(echo "$total_p99_9_lat + $p99_9_lat" | bc)
        total_sub_lat=$(echo "$total_sub_lat + $sub_lat" | bc)
        ((count++))
    done

    avg_mean_lat=$(echo "scale=2; $total_mean_lat / $count" | bc)
    avg_p99_9_lat=$(echo "scale=2; $total_p99_9_lat / $count" | bc)
    avg_sub_lat=$(echo "scale=2; $total_sub_lat / $count" | bc)

    # echo "output the result to $output_file"
    echo "$workload_type,$io_depth,$block_size,$jobn,$avg_sub_lat,$avg_mean_lat,$avg_p99_9_lat,$total_iops,$total_bw,,${local_stats}",,"${remote_stats}" >> "$output_file"

    for device in "${devices[@]}"; do
        output_file_="${device//\//_}.fio_out"
        rm "$output_file_"
    done
}

# Main execution loop
for workload_type in "${workload_types[@]}"; do
    for io_depth in "${io_depths[@]}"; do
        for jobn in "${jobs[@]}"; do
            for block_size in "${block_sizes[@]}"; do
                printf "Running fio for workload type=%s, iodepth=%d, block size=%s...\n" "$workload_type" "$io_depth" "$block_size"

                # Start run.sh script and get its PID
                run_rerun_script

                #make a string username@ip
                target="$username@$target_ip"
                run_remote_rerun_script $target

                for device in "${devices[@]}"; do
                    run_fio "$device" "$workload_type" "$io_depth" "$block_size" "$jobn"
                done

                wait

                echo "FIO runs completed."

                # Capture and print the content in the log file
                local_output=$(stop_rerun_script)
                # echo "local_output is $local_output"
                remote_output=$(stop_remote_rerun_script $target)

                # Parse local run.sh output
                local_stats=$(extract_latency_breakdown "$local_output" "$workload_type" "$block_size")
                remote_stats=$(extract_latency_breakdown "$remote_output" "$workload_type" "$block_size")
                echo "Extracted local stats: $local_stats"  # Debugging statement

                echo "extracted remote stats: $remote_stats"

                extract_and_summarize_metrics "$workload_type" "$io_depth" "$block_size" "$jobn"
            done
        done
    done
done

# Clean up the log file if needed
# rm "$log_file"

printf "Experiments completed. Results saved to %s.\n" "$output_file"
