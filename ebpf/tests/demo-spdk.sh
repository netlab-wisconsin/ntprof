#!/usr/bin/env bash
# A tour of ntprof v2 with SPDK on one Linux machine, without SSD or NIC: SPDK's
# NVMe/TCP target (nvmf_tgt) exporting a null bdev on 127.0.0.1
# (tests/spdk-target.sh); SPDK's initiator (spdk_nvme_perf) and the kernel's
# (nvme-tcp with fio) as workloads.
#
#   tests/demo-spdk.sh         run all steps
#   tests/demo-spdk.sh 2       run one step
#   tests/demo-spdk.sh down    stop the target, disconnect the kernel initiator
#
#   1. SPDK initiator -> SPDK target: where a 4 KiB read spends its time,
#      against the latency spdk_nvme_perf measures itself
#   2. a slow device: 50 us added in front of the target's bdev shows up in
#      S7-S9 (ntprof compare)
#   3. kernel initiator -> SPDK target, profiled together
#
# Needs: a built ./ntprof (make); SPDK v26.05 built with ebpf/spdk's patch and
# --with-usdt in $SPDK (default ~/spdk); hugepages (tests/spdk-target.sh reserves
# HUGEMEM MB, default 4096); for step 3 fio, nvme-cli and the stock nvme-tcp
# module. The target polls on core 1 (CPUMASK=0x2), perf on core 4
# (PERF_MASK=0x10), fio on CPU 2 (CPU). Files go to $OUT (default
# /tmp/ntprof-demo-spdk). Each command is printed before it runs.
set -euo pipefail
cd "$(dirname "$0")/.."

SPDK=${SPDK:-$HOME/spdk}
export SPDK
OUT=${OUT:-/tmp/ntprof-demo-spdk}
PERF=$SPDK/build/bin/spdk_nvme_perf
TGT_BIN=$SPDK/build/bin/nvmf_tgt
PERF_MASK=${PERF_MASK:-0x10}
CPU=${CPU:-2}
NQN=${NQN:-nqn.2026-10.edu.wisc.ntprof:spdk-tcp}
SPDK_ARGS=(--spdk-host "$PERF" --spdk-target "$TGT_BIN")

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
run() {
    printf '\033[2m$ %s\033[0m\n' "$*"
    "$@"
}

target() { # up [delay_us] | down
    printf '\033[2m$ tests/spdk-target.sh %s\033[0m\n' "$*"
    tests/spdk-target.sh "$@" > /dev/null
}

# spdk_nvme_perf in the background, already connected when this returns:
# perf_bg NAME SECONDS. ntprof attaches to running SPDK processes only.
perf_bg() {
    printf '\033[2m$ spdk_nvme_perf -q 1 -o 4096 -w randread -t %s ... (in the background)\033[0m\n' "$2"
    sudo "$PERF" -q 1 -o 4096 -w randread -t "$2" -c "$PERF_MASK" -r "$(tests/spdk-target.sh trid)" \
        < /dev/null > "$OUT/$1.perf" 2>&1 &
    PERF_PID=$!
    sleep 3
}

# spdk_nvme_perf's own mean latency (its "Total" line, in us)
perf_mean() { awk '/^Total/ {print $(NF-2)}' "$OUT/$1.perf"; }

setup() {
    mkdir -p "$OUT"
    for f in "$PERF" "$TGT_BIN"; do
        if ! readelf -n "$f" 2> /dev/null | grep -q ntprof; then
            echo "$f: not found, or built without the ntprof probes (ebpf/spdk/README.md)" >&2
            exit 1
        fi
    done
    say "Test bed: SPDK nvmf_tgt with a null bdev on 127.0.0.1:4421 (no SSD, no NIC)"
}

record_base() {
    target down
    target up 0
    perf_bg base 16
    run sudo ./ntprof record -r both -s 10 -d 8 -i 4 "${SPDK_ARGS[@]}" -o "$OUT/base.ntp"
    wait "$PERF_PID" || true
}

step1() {
    say "1. SPDK initiator -> SPDK target: where a 4 KiB read spends its time"
    echo "   SPDK runs NVMe/TCP in user space, where kernel hooks see nothing. The patched SPDK"
    echo "   timestamps the commands ntprof samples and hands the times over in one USDT probe per"
    echo "   command and side; ntprof is told the two binaries. SPDK has no block layer: no S1."
    record_base
    echo "spdk_nvme_perf's own mean latency: $(perf_mean base) us"
}

step2() {
    say "2. A slow device: 50 us added in front of the target's bdev"
    echo "   The same workload with a delay bdev between the target and its null bdev. ntprof"
    echo "   compare puts the two runs side by side; the delay belongs in S7-S9."
    [[ -f $OUT/base.ntp ]] || record_base
    target down
    target up 50
    perf_bg delay 16
    run sudo ./ntprof record -r both -s 10 -d 8 --offline "${SPDK_ARGS[@]}" -o "$OUT/delay.ntp"
    wait "$PERF_PID" || true
    echo "spdk_nvme_perf's own mean latency: $(perf_mean delay) us"
    run ./ntprof compare "$OUT/base.ntp" "$OUT/delay.ntp"
    target down
}

step3() {
    say "3. Kernel initiator -> SPDK target"
    echo "   The kernel's nvme-tcp connects to the SPDK target; ntprof profiles the kernel"
    echo "   initiator with its kernel hooks and the target with SPDK's probes."
    target up 0
    sudo modprobe nvme-tcp
    run sudo nvme connect -t tcp -a 127.0.0.1 -s 4421 -n "$NQN"
    local dev=""
    for _ in $(seq 50); do
        for s in /sys/class/nvme-subsystem/nvme-subsys*; do
            [[ $(cat "$s/subsysnqn" 2> /dev/null) == "$NQN" ]] &&
                dev=$(ls "$s" | grep -E '^nvme[0-9]+n[0-9]+$' | head -1)
        done
        [[ -n $dev ]] && break
        sleep 0.2
    done
    [[ -n $dev ]] || { echo "the namespace did not appear" >&2; exit 1; }
    echo "namespace: /dev/$dev"
    printf '\033[2m$ fio --rw=randread --bs=4k --iodepth=1 --runtime=14 ... (in the background)\033[0m\n'
    sudo fio --name=kernel --filename="/dev/$dev" --rw=randread --bs=4k --iodepth=1 --direct=1 \
        --ioengine=libaio --time_based --runtime=14 --cpus_allowed="$CPU" \
        --output="$OUT/kernel.fio.txt" > /dev/null 2>&1 &
    local fio=$!
    run sudo ./ntprof record -r both -s 10 -d 8 --offline --session "$dev" --spdk-target "$TGT_BIN"
    wait "$fio" || true
    run sudo nvme disconnect -n "$NQN"
}

down() {
    sudo nvme disconnect -n "$NQN" > /dev/null 2>&1 || true
    tests/spdk-target.sh down
}

case "${1:-all}" in
    down) down ;;
    1 | 2 | 3) setup && "step$1" ;;
    all)
        setup
        step1
        step2
        step3
        target down
        say "Done. Record files are in $OUT."
        ;;
    *) echo "usage: $0 [1|2|3|all|down]" >&2; exit 2 ;;
esac
