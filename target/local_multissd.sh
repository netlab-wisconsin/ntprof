#!/bin/bash

output_file="fio_results.csv"

if [ -f "$output_file" ]; then
    read -p "$output_file exists. Remove it? (y/n): " answer
    if [ "$answer" = "y" ]; then
        rm "$output_file"
        echo "Removed $output_file."
    else
        echo "Continuing without removing $output_file."
    fi
fi

# workload_types=("write" "randwrite")
# io_depths=(1 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32)
workload_types=("read" "randread")
io_depths=(1 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32)
devices=("/dev/nvme0n1" "/dev/nvme1n1" "/dev/nvme2n1" "/dev/nvme3n1")
echo "Workload Type,I/O Depth,Block Size,Total Average Latency (usec),Total IOPS,Total Bandwidth (MB/s)" > "$output_file"

run_fio() {
    local device="$1"
    local workload_type="$2"
    local io_depth="$3"
    local block_size="$4"
    local output_file="${device//\//_}.fio_out" # 根据设备名称创建输出文件名
    sudo fio --name=test --filename="$device" --rw="$workload_type" --ioengine=libaio --numjobs=8 --time_based --runtime=20 --group_reporting --bs="$block_size" --direct=1 --iodepth="$io_depth" > "$output_file" &
}

extract_and_summarize_metrics() {
    local workload_type="$1"
    local io_depth="$2"
    local block_size="$3"
    local total_iops=0
    local total_bw=0
    local total_lat=0
    local count=0
    
    for device in "${devices[@]}"; do
        local output_file_="${device//\//_}.fio_out"
        local fio_output=$(cat "$output_file_")

        local iops=$(echo "$fio_output" | grep -oP 'IOPS=\K[\d.]+k?' | tr -d 'K' | awk '{print ($0+0) * (/k$/ ? 1000 : 1)}')
        local bw_mbs=$(echo "$fio_output" | grep -oP 'BW=\K.*?\(\K\d+(\.\d+)?(?=MB/s\))')
        local avg_lat_usec=$(echo "$fio_output" | grep -Po ' lat \(usec\):.*avg=\K\d+(\.\d+)?')
        
        # If not found, try to extract in milliseconds (msec) and convert to usec
        if [ -z "$avg_lat_usec" ]; then
            local avg_lat_msec=$(echo "$fio_output" | grep -Po ' lat \(msec\):.*avg=\K\d+(\.\d+)?')
            if [ -n "$avg_lat_msec" ]; then
                # Convert msec to usec
                avg_lat_usec=$(echo "$avg_lat_msec" | awk '{print $1 * 1000}')
            else
                printf "Error: Average latency unit not recognized.\n"
                printf "Raw fio output:\n"
                printf "%s" "$fio_output"
                return 1 # Return with error
            fi
        else
            avg_lat_usec=$avg_lat_usec
        fi
        
        total_iops=$((total_iops + iops))
        total_bw=$(echo "$total_bw + $bw_mbs" | bc)
        total_lat=$(echo "$total_lat + $avg_lat_usec" | bc)
        ((count++))
    done

    local avg_lat=$(echo "scale=2; $total_lat / $count" | bc)

    echo "$workload_type,$io_depth,$block_size,$avg_lat,$total_iops,$total_bw" >> "$output_file"
    
    # 清理临时文件
    for device in "${devices[@]}"; do
        local output_file="${device//\//_}.fio_out"
        rm "$output_file"
    done
}

for workload_type in "${workload_types[@]}"; do
    for io_depth in "${io_depths[@]}"; do
        block_size="4K"
        if [ "$workload_type" == "read" ]; then
            block_size="128K"
        fi

        printf "Running fio for workload type=%s, iodepth=%d, block size=%s...\n" "$workload_type" "$io_depth" "$block_size"
        for device in "${devices[@]}"; do
            run_fio "$device" "$workload_type" "$io_depth" "$block_size"
        done

        # 等待所有fio任务完成
        wait

        extract_and_summarize_metrics "$workload_type" "$io_depth" "$block_size"
    done
done

printf "Experiments completed. Results saved to %s.\n" "$output_file"
