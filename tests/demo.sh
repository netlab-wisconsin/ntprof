#!/usr/bin/env bash
# A tour of ntprof on one Linux machine, without SSD or NIC: a loopback
# NVMe/TCP namespace on null_blk (tests/loopback.sh), fio as the workload.
#
#   tests/demo.sh            run all steps
#   tests/demo.sh 2          run one step (the test bed is set up and kept)
#   tests/demo.sh down       remove the test bed
#
#   1. live breakdown, host and target profiled together
#   2. the two sides recorded separately (as on two machines), joined offline
#   3. a bottleneck search: LAD per stage between queue depth 1 and 32
#
# Needs: a built ./ntprof (make), fio, nvme-cli, sudo, and the stock modules
# nvme-tcp, nvmet-tcp and null_blk (loaded by tests/loopback.sh). Files go to
# $OUT (default /tmp/ntprof-demo). Each command is printed before it runs.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${OUT:-/tmp/ntprof-demo}
CPU=${CPU:-2} # the CPU fio runs on

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
run() {
    printf '\033[2m$ %s\033[0m\n' "$*"
    "$@"
}

# fio in the background: fio_bg NAME QD SECONDS
fio_bg() {
    printf '\033[2m$ fio --rw=randread --bs=4k --iodepth=%s --runtime=%s ... (in the background)\033[0m\n' "$2" "$3"
    sudo fio --name="$1" --filename="$DEV" --rw=randread --bs=4k --iodepth="$2" --direct=1 \
        --ioengine=libaio --time_based --runtime="$3" --cpus_allowed="$CPU" \
        --output="$OUT/$1.fio.txt" > /dev/null 2>&1 &
    FIO=$!
}

# ntprof in the background until it is attached: ntprof_bg LOG ARGS...
ntprof_bg() {
    local log=$1
    shift
    printf '\033[2m$ sudo ./ntprof record %s (in the background)\033[0m\n' "$*"
    sudo ./ntprof record "$@" > "$log" 2>&1 &
    NTP_PIDS+=($!)
    until grep -q "profiling as" "$log" 2> /dev/null; do
        if ! kill -0 "${NTP_PIDS[-1]}" 2> /dev/null; then
            cat "$log" >&2
            exit 1
        fi
        sleep 0.2
    done
}

setup() {
    mkdir -p "$OUT"
    say "Test bed: null_blk exported by nvmet-tcp on 127.0.0.1 and connected with nvme-tcp"
    DEV=/dev/$(tests/loopback.sh tcp up)
    echo "namespace: $DEV"
}

step1() {
    say "1. Live breakdown: 4 KiB random reads at queue depth 1, both sides on this machine"
    echo "   ntprof records 1 command in 10, prints a line every 5 s and a report at the end; the"
    echo "   target's records are joined with the host's as they arrive."
    fio_bg live 1 30
    run sudo ./ntprof record -r both -s 10 -d 10 -i 5
    sudo kill "$FIO" 2> /dev/null || true
    wait "$FIO" 2> /dev/null || true
}

step2() {
    say "2. The two sides recorded separately, as on two machines, and joined offline"
    echo "   On real hardware the first command runs on the target host and the second on the"
    echo "   host. Both use the same sampling period (-s), so they pick the same commands;"
    echo "   start the target's recording first and let it end last, so every host record"
    echo "   finds its target record."
    NTP_PIDS=()
    ntprof_bg "$OUT/target.log" -r target -s 10 -d 14 -o "$OUT/target.ntp"
    ntprof_bg "$OUT/host.log" -r host -s 10 -d 8 -o "$OUT/host.ntp"
    fio_bg two 1 8
    wait "$FIO" || true
    for p in "${NTP_PIDS[@]}"; do wait "$p" || true; done
    run ./ntprof report "$OUT/host.ntp" "$OUT/target.ntp"
}

step3() {
    say "3. Which stage amplifies latency under load? LAD between queue depth 1 and 32"
    echo "   LAD of a stage = its mean latency loaded / unloaded; the largest one is the bottleneck."
    for qd in 1 32; do
        NTP_PIDS=()
        ntprof_bg "$OUT/qd$qd.log" -r both -s 10 -d 8 --offline -o "$OUT/qd$qd.ntp"
        fio_bg "qd$qd" "$qd" 8
        wait "$FIO" || true
        wait "${NTP_PIDS[0]}" || true
        grep -E "^throughput" "$OUT/qd$qd.log" || true
    done
    run ./ntprof compare "$OUT/qd1.ntp" "$OUT/qd32.ntp"
}

case "${1:-all}" in
    down) tests/loopback.sh tcp down ;;
    1 | 2 | 3) setup && "step$1" ;;
    all)
        setup
        step1
        step2
        step3
        say "Done. Record files are in $OUT; remove the test bed with: tests/demo.sh down"
        ;;
    *) echo "usage: $0 [1|2|3|all|down]" >&2; exit 2 ;;
esac
