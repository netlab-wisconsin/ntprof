#!/bin/bash

output_file="fio_results.csv"

if [ -f "$output_file" ]; then
    # read -p "$output_file exists. Remove it? (y/n): " answer
    # if [ "$answer" = "y" ]; then
        rm "$output_file"
    #     echo "Removed $output_file."
    # else
    #     echo "Continuing without removing $output_file."
    # fi
fi

# workload_types=("write" "randwrite")
# io_depths=(1 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32)
workload_types=("randread")
# io_depths=(16 24 32 40 48 56 64 72 80 88 96)
io_depths=(16 24 32 40 48 56 64 72 80 88 96 100)
jobs=(16 24 32 40 48 56 64 72 80 88 96 100)
# devices=("/dev/nvme4n1" "/dev/nvme5n1" "/dev/nvme6n1" "/dev/nvme7n1")
# devices=("/dev/nvme4n1" "/dev/nvme4n2" "/dev/nvme4n3" "/dev/nvme4n4")
devices=("/dev/nvme4n1" "/dev/nvme4n2" "/dev/nvme4n3")
echo "Workload,io_depth,bk,jobn,lat(usec),iops,bw(MB/s)" > "$output_file"

run_fio() {
    local device="$1"
    local workload_type="$2"
    local io_depth="$3"
    local block_size="$4"
    local jobn="$5"
    local output_file="${device//\//_}.fio_out" # 根据设备名称创建输出文件名
    sudo fio --name=test --filename="$device" --rw="$workload_type" --ioengine=libaio --numjobs=$jobn --time_based --runtime=20 --group_reporting --bs="$block_size" --direct=1 --iodepth="$io_depth" > "$output_file" &
}

extract_and_summarize_metrics() {
    local workload_type="$1"
    local io_depth="$2"
    local block_size="$3"
    local jobn="$4"
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
        printf "Device=%s, IOPS=%d, BW=%s MB/s, Avg. Latency=%s usec\n" "$device" "$iops" "$bw_mbs" "$avg_lat_usec"
        total_iops=$((total_iops + iops))
        total_bw=$(echo "$total_bw + $bw_mbs" | bc)
        total_lat=$(echo "$total_lat + $avg_lat_usec" | bc)
        ((count++))
    done

    local avg_lat=$(echo "scale=2; $total_lat / $count" | bc)

    echo "$workload_type,$io_depth,$block_size,$jobn,$avg_lat,$total_iops,$total_bw" >> "$output_file"
    
    # 清理临时文件
    for device in "${devices[@]}"; do
        local output_file="${device//\//_}.fio_out"
        rm "$output_file"
    done
}

for workload_type in "${workload_types[@]}"; do
    for io_depth in "${io_depths[@]}"; do
        for jobn in "${jobs[@]}"; do
            block_size="4K"
            if [ "$workload_type" == "read" ]; then
                block_size="128K"
            fi

            printf "Running fio for workload type=%s, iodepth=%d, block size=%s...\n" "$workload_type" "$io_depth" "$block_size"
            for device in "${devices[@]}"; do
                run_fio "$device" "$workload_type" "$io_depth" "$block_size" "$jobn"
            done

            # 等待所有fio任务完成
            wait

            extract_and_summarize_metrics "$workload_type" "$io_depth" "$block_size" "$jobn"
        done
    done
done

printf "Experiments completed. Results saved to %s.\n" "$output_file"
