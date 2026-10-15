#!/usr/bin/env bash
# Set up or tear down an NVMe-oF loopback test bed: a null_blk device exported
# by the kernel target and connected to on the same machine. Uses stock
# modules and configfs only; no SSD is involved.
#
#   loopback.sh tcp  up [completion_ns] [io_queues]   prints the namespace device
#   loopback.sh rdma up [completion_ns] [io_queues]
#   loopback.sh tcp|rdma down
#   loopback.sh tcp|rdma dev                           prints the namespace device
#
# On two machines (or guests), the same pieces apart:
#   loopback.sh tcp|rdma export [completion_ns]        on the target: null_blk exported at ADDR
#   loopback.sh tcp|rdma connect ADDR [io_queues]      on the host; prints the namespace device
# NETDEV / ADDR name the interface and address (default: the default route's);
# NULLB_MBPS > 0 limits null_blk's bandwidth (MB/s), a stand-in for a busy drive.
#
# tcp runs over 127.0.0.1. rdma runs over soft-RoCE (rdma_rxe) bound to
# NETDEV (default: the interface of the default route) at its IPv4 address:
# the traffic loops back inside rxe and never leaves the machine, but rxe is
# attached to that interface, so use it only on a machine you may configure.
#
# completion_ns > 0 makes null_blk complete each request after that many ns
# (timer mode), which adds a known latency to the target's device stage.
set -euo pipefail

TRANSPORT=${1:-}
ACTION=${2:-}
case "$TRANSPORT" in
    tcp | rdma) ;;
    *) echo "usage: $0 tcp|rdma up|export|connect|down|dev ..." >&2; exit 2 ;;
esac
shift 2 || true

[[ $(id -u) -eq 0 ]] || exec sudo -E "$0" "$TRANSPORT" "$ACTION" "$@"

NQN=${NQN:-nqn.2026-10.edu.wisc.ntprof:loopback-$TRANSPORT}
SVC=${SVC:-4420}
NULLB=ntp_$TRANSPORT
NVMET=/sys/kernel/config/nvmet
NULLB_CFG=/sys/kernel/config/nullb
RXE=${RXE:-ntp_rxe0}
if [[ $TRANSPORT == tcp ]]; then
    PORT_ID=${PORT_ID:-1}
    ADDR=${ADDR:-127.0.0.1}
else
    PORT_ID=${PORT_ID:-2}
    NETDEV=${NETDEV:-$(ip -o route get 1.1.1.1 | sed -n 's/.* dev \([^ ]*\).*/\1/p')}
    ADDR=${ADDR:-$(ip -4 -o addr show dev "$NETDEV" | awk '{print $4}' | cut -d/ -f1 | head -1)}
fi

namespace_dev() {
    local s ns
    for s in /sys/class/nvme-subsystem/nvme-subsys*; do
        [[ -e $s/subsysnqn && $(cat "$s/subsysnqn") == "$NQN" ]] || continue
        # the namespace head: nvme<N>n1 (no path letter); the subsystem can
        # appear before its namespace does
        ns=$(ls "$s" | grep -E '^nvme[0-9]+n[0-9]+$' | head -1)
        [[ -n $ns ]] || return 1
        echo "$ns"
        return 0
    done
    return 1
}

up() {
    local completion_ns=${1:-0} queues=${2:-4}

    modprobe null_blk nr_devices=0
    modprobe nvmet
    modprobe "nvmet-$TRANSPORT"
    modprobe "nvme-$TRANSPORT"
    # a soft-RoCE device on NETDEV, unless the machine already has one there
    if [[ $TRANSPORT == rdma ]] && ! rdma link show | grep -q "netdev $NETDEV\b"; then
        modprobe rdma_rxe
        rdma link add "$RXE" type rxe netdev "$NETDEV"
    fi

    if [[ ! -d $NULLB_CFG/$NULLB ]]; then
        mkdir "$NULLB_CFG/$NULLB"
        echo 4096 > "$NULLB_CFG/$NULLB/size"          # MB; not memory backed
        echo "$queues" > "$NULLB_CFG/$NULLB/submit_queues"
        echo 2 > "$NULLB_CFG/$NULLB/queue_mode"      # blk-mq
        if ((completion_ns > 0)); then
            echo 2 > "$NULLB_CFG/$NULLB/irqmode"     # timer
            echo "$completion_ns" > "$NULLB_CFG/$NULLB/completion_nsec"
        else
            echo 1 > "$NULLB_CFG/$NULLB/irqmode"     # softirq
        fi
        if ((${NULLB_MBPS:-0} > 0)); then
            echo "$NULLB_MBPS" > "$NULLB_CFG/$NULLB/mbps"
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
        echo "$TRANSPORT" > "$NVMET/ports/$PORT_ID/addr_trtype"
        echo ipv4 > "$NVMET/ports/$PORT_ID/addr_adrfam"
        echo "$ADDR" > "$NVMET/ports/$PORT_ID/addr_traddr"
        echo "$SVC" > "$NVMET/ports/$PORT_ID/addr_trsvcid"
    fi
    [[ -e $NVMET/ports/$PORT_ID/subsystems/$NQN ]] ||
        ln -s "$NVMET/subsystems/$NQN" "$NVMET/ports/$PORT_ID/subsystems/$NQN"
}

connect() { # addr queues
    modprobe "nvme-$TRANSPORT"
    [[ -s /etc/nvme/hostnqn ]] || { mkdir -p /etc/nvme; nvme gen-hostnqn > /etc/nvme/hostnqn; }
    if ! namespace_dev > /dev/null; then
        nvme connect -t "$TRANSPORT" -a "$1" -s "$SVC" -n "$NQN" -i "$2" > /dev/null
        for _ in $(seq 150); do namespace_dev > /dev/null && break; sleep 0.1; done
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
    if [[ $TRANSPORT == rdma ]] && rdma link show "$RXE/1" > /dev/null 2>&1; then
        rdma link delete "$RXE"
    fi
    return 0
}

case "$ACTION" in
    up)
        up "${1:-0}" "${2:-4}"
        connect "$ADDR" "${2:-4}"
        ;;
    export) up "${1:-0}" 4 ;;
    connect) connect "${1:?connect ADDR [io_queues]}" "${2:-4}" ;;
    down) down ;;
    dev) namespace_dev ;;
    *)
        echo "usage: $0 tcp|rdma up [completion_ns] [io_queues] | export [completion_ns] |" \
            "connect ADDR [io_queues] | down | dev" >&2
        exit 2
        ;;
esac
