#!/usr/bin/env bash
# Set up or tear down an NVMe/TCP loopback test bed: a null_blk device exported
# by the kernel target (nvmet-tcp) on 127.0.0.1 and connected with nvme-tcp.
# Uses only stock modules and configfs; no SSD and no NIC are involved.
#
#   loopback-tcp.sh up [completion_ns] [io_queues]   prints the namespace device
#   loopback-tcp.sh down
#   loopback-tcp.sh dev                               prints the namespace device
#
# completion_ns > 0 makes null_blk complete each request after that many ns
# (timer mode), which adds a known latency to the target's device stage.
set -euo pipefail

NQN=${NQN:-nqn.2026-10.edu.wisc.ntprof:loopback}
ADDR=${ADDR:-127.0.0.1}
SVC=${SVC:-4420}
PORT_ID=${PORT_ID:-1}
NULLB=ntp0
NVMET=/sys/kernel/config/nvmet
NULLB_CFG=/sys/kernel/config/nullb

[[ $(id -u) -eq 0 ]] || exec sudo -E "$0" "$@"

namespace_dev() {
    local s
    for s in /sys/class/nvme-subsystem/nvme-subsys*; do
        [[ -e $s/subsysnqn && $(cat "$s/subsysnqn") == "$NQN" ]] || continue
        # the namespace head: nvme<N>n1 (no path letter)
        ls "$s" | grep -E '^nvme[0-9]+n[0-9]+$' | head -1
        return 0
    done
    return 1
}

up() {
    local completion_ns=${1:-0} queues=${2:-4}

    modprobe null_blk nr_devices=0
    modprobe nvmet
    modprobe nvmet-tcp
    modprobe nvme-tcp

    if [[ ! -d $NULLB_CFG/$NULLB ]]; then
        mkdir "$NULLB_CFG/$NULLB"
        echo 4096 > "$NULLB_CFG/$NULLB/size"          # MB; not memory backed
        echo "$queues" > "$NULLB_CFG/$NULLB/submit_queues"
        echo 2 > "$NULLB_CFG/$NULLB/queue_mode"      # blk-mq
        if (( completion_ns > 0 )); then
            echo 2 > "$NULLB_CFG/$NULLB/irqmode"     # timer
            echo "$completion_ns" > "$NULLB_CFG/$NULLB/completion_nsec"
        else
            echo 1 > "$NULLB_CFG/$NULLB/irqmode"     # softirq
        fi
        echo 1 > "$NULLB_CFG/$NULLB/power"
    fi
    # newer kernels name configfs devices after the directory, older ones nullb<index>
    local nullb_dev=/dev/$NULLB
    [[ -b $nullb_dev ]] || nullb_dev=/dev/nullb$(cat "$NULLB_CFG/$NULLB/index")

    local ns=$NVMET/subsystems/$NQN/namespaces/1
    if [[ ! -d $NVMET/subsystems/$NQN ]]; then
        mkdir "$NVMET/subsystems/$NQN"
        echo 1 > "$NVMET/subsystems/$NQN/attr_allow_any_host"
    fi
    [[ -d $ns ]] || mkdir "$ns"
    if [[ $(cat "$ns/enable") != 1 ]]; then
        echo -n "$nullb_dev" > "$ns/device_path"
        echo 1 > "$ns/enable"
    fi
    if [[ ! -d $NVMET/ports/$PORT_ID ]]; then
        mkdir "$NVMET/ports/$PORT_ID"
        echo tcp > "$NVMET/ports/$PORT_ID/addr_trtype"
        echo ipv4 > "$NVMET/ports/$PORT_ID/addr_adrfam"
        echo "$ADDR" > "$NVMET/ports/$PORT_ID/addr_traddr"
        echo "$SVC" > "$NVMET/ports/$PORT_ID/addr_trsvcid"
    fi
    [[ -e $NVMET/ports/$PORT_ID/subsystems/$NQN ]] ||
        ln -s "$NVMET/subsystems/$NQN" "$NVMET/ports/$PORT_ID/subsystems/$NQN"

    [[ -s /etc/nvme/hostnqn ]] || { mkdir -p /etc/nvme; nvme gen-hostnqn > /etc/nvme/hostnqn; }
    if ! namespace_dev > /dev/null; then
        nvme connect -t tcp -a "$ADDR" -s "$SVC" -n "$NQN" -i "$queues" > /dev/null
        for _ in $(seq 50); do namespace_dev > /dev/null && break; sleep 0.1; done
    fi
    namespace_dev
}

down() {
    nvme disconnect -n "$NQN" > /dev/null 2>&1 || true
    rm -f "$NVMET/ports/$PORT_ID/subsystems/$NQN"
    [[ -d $NVMET/ports/$PORT_ID ]] && rmdir "$NVMET/ports/$PORT_ID"
    if [[ -d $NVMET/subsystems/$NQN ]]; then
        if [[ -d $NVMET/subsystems/$NQN/namespaces/1 ]]; then
            echo 0 > "$NVMET/subsystems/$NQN/namespaces/1/enable"
            rmdir "$NVMET/subsystems/$NQN/namespaces/1"
        fi
        rmdir "$NVMET/subsystems/$NQN"
    fi
    if [[ -d $NULLB_CFG/$NULLB ]]; then
        echo 0 > "$NULLB_CFG/$NULLB/power"
        rmdir "$NULLB_CFG/$NULLB"
    fi
}

case "${1:-}" in
    up) shift; up "$@" ;;
    down) down ;;
    dev) namespace_dev ;;
    *) echo "usage: $0 up [completion_ns] [io_queues] | down | dev" >&2; exit 2 ;;
esac
