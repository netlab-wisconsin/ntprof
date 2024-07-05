#!/bin/bash

output_file="fio_result.csv"

# Check if file exists
if [ -f "$output_file" ]; then
    read -p "$output_file exists. Remove it? (y/n): " answer
    if [ "$answer" = "y" ]; then
        rm "$output_file"
        echo "Removed $output_file."
    else
        echo "Continuing without removing $output_file."
    fi
fi

# Setup parameters
# workloads=("read" "randread" "write" "randwrite")
# iodepths=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
# jobs=(1)
# devices=("/dev/nvme0n1" "/dev/nvme1n1" "/dev/nvme2n1" "/dev/nvme3n1")
# runtime=20;

workloads=("randread")
iodepths=(32)
jobs=(1)
devices=("/dev/nvme0n1")
runtime=60;
blocksizes=("4K")

echo "workload, jobs, iodepth, blocksize, meanlat(us), p999lat(us), iops, bw(MB/s)" > "$output_file"

# Function to run fio
run_fio() {
    local device="$1"
    local workload="$2"
    local job="$3"
    local iodepth="$4"
    local bs="$5"
    local output_file="${device//\//_}.fio_out"
    local cmd="sudo fio --name=test --filename=$device --size=20G --direct=1 --time_based --runtime=$runtime --cpus_allowed=0,2,4,6,8,10,12,14,17,21,25,29 --cpus_allowed_policy=split --ioengine=libaio --group_reporting --output-format=terse --rw=$workload --numjobs=$job --bs=$bs --iodepth=$iodepth"
    
    echo "$cmd"
    eval "$cmd" > "$output_file" &
}

# Function to extract metrics
extract_and_summarize_metrics() {
    local workload="$1"
    local job="$2"
    local iodepth="$3"
    local bs="$4"
    local total_iops=0
    local total_bw=0
    local total_meanlat=0
    local total_p999lat=0
    local count=0

    for device in "${devices[@]}"; do
        local output_file_="${device//\//_}.fio_out"
        local fio_output=$(cat "$output_file_")
        local meanlat p999lat iops bw

        IFS=';' read -ra metrics <<< "$fio_output"

        if [[ "$workload" == "read" || "$workload" == "randread" ]]; then
            meanlat="${metrics[15]}"
            p999lat="${metrics[31]}"
            p999lat="${p999lat#*=}"
            iops="${metrics[7]}"
            bw="${metrics[6]}"
        elif [[ "$workload" == "write" || "$workload" == "randwrite" ]]; then
            meanlat="${metrics[56]}"
            p999lat="${metrics[72]}"
            p999lat="${p999lat#*=}"
            iops="${metrics[48]}"
            bw="${metrics[47]}"
        else
            echo "Unknown workload type $workload"
            return 1
        fi

        bw=$(bc <<< "scale=3; $bw * 0.001024")
        total_iops=$((total_iops + iops))
        total_bw=$(echo "$total_bw + $bw" | bc)
        total_meanlat=$(echo "$total_meanlat + $meanlat" | bc)
        total_p999lat=$((total_p999lat + p999lat))
        ((count++))
    done

    local avg_meanlat=$(echo "scale=2; $total_meanlat / $count" | bc)
    local avg_p999lat=$(echo "scale=2; $total_p999lat / $count" | bc)

    echo "$workload,$job,$iodepth,$bs,$avg_meanlat,$avg_p999lat,$total_iops,$total_bw" >> "$output_file"

    for device in "${devices[@]}"; do
        local output_file="${device//\//_}.fio_out"
        rm "$output_file"
    done
}

# Main experiment loop
for workload in "${workloads[@]}"; do
  for bs in "${blocksizes[@]}"; do
  # if workload contains "random", then bs = 4K
#   local bs
#   if [[ "$workload" == *"rand"* ]]; then
#     bs="4K"
#   else
#     bs="128K"
#   fi
    for job in "${jobs[@]}"; do
        for iodepth in "${iodepths[@]}"; do
            echo "Running fio for workload=$workload, jobs=$job, iodepth=$iodepth, blocksize=$bs ...\n" 
            for device in "${devices[@]}"; do
                run_fio "$device" "$workload" "$job" "$iodepth" "$bs"
            done

            wait
            extract_and_summarize_metrics "$workload" "$job" "$iodepth" "$bs"
        done
    done
  done
done

printf "Experiments completed. Results saved to %s.\n" "$output_file"
