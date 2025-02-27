#!/bin/bash

# Check input arguments
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output_svg_filename>"
    exit 1
fi

# Parse argument
OUTPUT_SVG=$1

# Define cleanup function to handle Ctrl+C (SIGINT)
cleanup() {
    echo "Interrupt detected! Cleaning up..."
    sudo pkill -P $$  # Kill all subprocesses
    exit 1
}

# Catch SIGINT (Ctrl+C) and trigger cleanup
trap cleanup SIGINT

# Run perf recording for 60 seconds (overwrite old perf.data)
echo "Running perf record for 60 seconds..."
sudo perf record -F 99 -a -g -- sleep 60

# Convert perf.data to a readable format
echo "Converting perf.data to human-readable format..."
sudo perf script > out.perf

# Process with FlameGraph scripts
echo "Collapsing stack traces..."
./FlameGraph/stackcollapse-perf.pl out.perf > out.folded

echo "Generating flame graph..."
./FlameGraph/flamegraph.pl out.folded > "$OUTPUT_SVG"

# Transfer the generated SVG file
echo "Transferring $OUTPUT_SVG ..."
sudo sz "$OUTPUT_SVG"

echo "Process completed! Flame graph saved as $OUTPUT_SVG."

