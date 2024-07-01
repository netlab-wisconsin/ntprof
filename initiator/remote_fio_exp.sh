#!/bin/bash

output_file="fio_results.csv"

if [ -f "$output_file" ]; then
    rm "$output_file"
fi

workload_types=("randread")
io_depths=(32)
jobs=(1)
devices=("/dev/nvme4n1")
block_sizes=("4K")
# block_sizes=("4K")
echo "Workload,io_depth,block_size,jobn,mean_lat(usec),p999lat(usec),iops,bw(MB/s)" > "$output_file"

run_fio() {
    local device="$1"
    local workload_type="$2"
    local io_depth="$3"
    local block_size="$4"
    local jobn="$5"
    local output_file="${device//\//_}.fio_out"
    # echo the command
    echo "sudo fio --name=test --filename=$device --size=20G --direct=1 --time_based --runtime=60 --cpus_allowed=0 --cpus_allowed_policy=split --ioengine=libaio --group_reporting --rw=$workload_type --numjobs=$jobn --bs=$block_size --iodepth=$io_depth"

    sudo fio --name=test --filename="$device" --size=20G --direct=1 --time_based --runtime=60 --cpus_allowed=0 --cpus_allowed_policy=split --ioengine=libaio --group_reporting --output-format=terse --rw="$workload_type" --numjobs=$jobn --bs="$block_size" --iodepth="$io_depth" > "$output_file" &
}

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

    for device in "${devices[@]}"; do
        local output_file_="${device//\//_}.fio_out"
        local fio_output=$(cat "$output_file_")

        if [ "$workload_type" == "randread" ]; then
            local bw_kib=$(echo "$fio_output" | cut -d';' -f7)
            local iops=$(echo "$fio_output" | cut -d';' -f8)
            local mean_lat=$(echo "$fio_output" | cut -d';' -f16)
            local p99_9_lat=$(echo "$fio_output" | cut -d';' -f32 | cut -d'=' -f2)
        else
            local bw_kib=$(echo "$fio_output" | cut -d';' -f48)
            local iops=$(echo "$fio_output" | cut -d';' -f49)
            local mean_lat=$(echo "$fio_output" | cut -d';' -f57)
            local p99_9_lat=$(echo "$fio_output" | cut -d';' -f73 | cut -d'=' -f2)
        fi

        local bw_mbs=$(echo "scale=2; $bw_kib / 1024" | bc)
        printf "device=%s, iops=%d, bw=%s MB/s, meanlat=%s usec, p999lat=%s usec\n" "$device" "$iops" "$bw_mbs" "$mean_lat" "$p99_9_lat"
        total_iops=$((total_iops + iops))
        total_bw=$(echo "$total_bw + $bw_mbs" | bc)
        total_mean_lat=$(echo "$total_mean_lat + $mean_lat" | bc)
        total_p99_9_lat=$(echo "$total_p99_9_lat + $p99_9_lat" | bc)
        ((count++))
    done

    local avg_mean_lat=$(echo "scale=2; $total_mean_lat / $count" | bc)
    local avg_p99_9_lat=$(echo "scale=2; $total_p99_9_lat / $count" | bc)

    echo "$workload_type,$io_depth,$block_size,$jobn,$avg_mean_lat,$avg_p99_9_lat,$total_iops,$total_bw" >> "$output_file"

    for device in "${devices[@]}"; do
        local output_file="${device//\//_}.fio_out"
        rm "$output_file"
    done
}

for workload_type in "${workload_types[@]}"; do
    for io_depth in "${io_depths[@]}"; do
        for jobn in "${jobs[@]}"; do
            for block_size in "${block_sizes[@]}"; do
                printf "Running fio for workload type=%s, iodepth=%d, block size=%s...\n" "$workload_type" "$io_depth" "$block_size"
                for device in "${devices[@]}"; do
                    run_fio "$device" "$workload_type" "$io_depth" "$block_size" "$jobn"
                done

                wait

                extract_and_summarize_metrics "$workload_type" "$io_depth" "$block_size" "$jobn"
            done
        done
    done
done

printf "Experiments completed. Results saved to %s.\n" "$output_file"
