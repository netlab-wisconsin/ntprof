#!/usr/bin/env bash
# Start or stop an SPDK NVMe/TCP or NVMe/RDMA target for tests: nvmf_tgt
# exporting a null bdev (optionally behind a delay bdev), or a block device.
#
#   spdk-target.sh up [delay_us]   start nvmf_tgt; prints the transport ID
#   spdk-target.sh down
#   spdk-target.sh trid            prints the transport ID (spdk_nvme_perf -r)
#
# SPDK      the SPDK tree, built with ntprof's spdk/ patch and --with-usdt
#           (default ~/spdk)
# CPUMASK   cores of nvmf_tgt's reactors (default 0x2)
# HUGEMEM   MB of 2 MB hugepages to have free before starting (default 4096)
# ADDR, SVC listener (default 127.0.0.1:4421; the kernel test bed uses 4420)
# TRTYPE    tcp (default) or rdma; rdma needs ADDR on an RDMA interface
# AIO       a block device to export through an AIO bdev instead of the null
#           bdev (a delay bdev goes over it all the same)
#
# delay_us > 0 puts a delay bdev over the null bdev: every read and write
# then spends that long in the target's backend (the storage stage).
set -euo pipefail

ACTION=${1:-}
shift || true
SPDK=${SPDK:-$HOME/spdk}
[[ $(id -u) -eq 0 ]] || exec sudo -E SPDK="$SPDK" "$0" "$ACTION" "$@"

TRTYPE=${TRTYPE:-tcp}
NQN=${NQN:-nqn.2026-10.edu.wisc.ntprof:spdk-$TRTYPE}
ADDR=${ADDR:-127.0.0.1}
AIO=${AIO:-}
SVC=${SVC:-4421}
CPUMASK=${CPUMASK:-0x2}
HUGEMEM=${HUGEMEM:-4096}
SOCK=/var/tmp/ntp_spdk_tgt.sock
PIDFILE=/var/tmp/ntp_spdk_tgt.pid
LOG=/var/tmp/ntp_spdk_tgt.log

rpc() { "$SPDK/scripts/rpc.py" -s "$SOCK" "$@"; }

trid() {
    echo "trtype:${TRTYPE^^} adrfam:IPv4 traddr:$ADDR trsvcid:$SVC subnqn:$NQN"
}

running() { [[ -s $PIDFILE ]] && kill -0 "$(cat "$PIDFILE")" 2> /dev/null; }

reserve_hugepages() {
    local free_kb page_kb
    free_kb=$(awk '/^HugePages_Free/ {f=$2} /^Hugepagesize/ {s=$2} END {print f*s}' /proc/meminfo)
    page_kb=$(awk '/^Hugepagesize/ {print $2}' /proc/meminfo)
    if ((free_kb < HUGEMEM * 1024)); then
        local total
        total=$(cat /proc/sys/vm/nr_hugepages)
        echo $((total + (HUGEMEM * 1024 - free_kb) / page_kb)) > /proc/sys/vm/nr_hugepages
    fi
}

up() {
    local delay_us=${1:-0} bdev=Null0

    case $TRTYPE in
        tcp | rdma) ;;
        *) echo "TRTYPE must be tcp or rdma" >&2; exit 2 ;;
    esac
    if [[ $TRTYPE == rdma && $ADDR == 127.0.0.1 ]]; then
        echo "TRTYPE=rdma needs ADDR, the address of an RDMA interface" >&2
        exit 2
    fi

    if running; then
        trid
        return 0
    fi
    reserve_hugepages
    rm -f "$SOCK"
    "$SPDK/build/bin/nvmf_tgt" -m "$CPUMASK" -r "$SOCK" < /dev/null > "$LOG" 2>&1 &
    echo $! > "$PIDFILE"
    for _ in $(seq 100); do
        [[ -S $SOCK ]] && rpc spdk_get_version > /dev/null 2>&1 && break
        running || { cat "$LOG" >&2; exit 1; }
        sleep 0.2
    done
    rpc nvmf_create_transport -t "${TRTYPE^^}" > /dev/null
    if [[ -n $AIO ]]; then
        rpc bdev_aio_create "$AIO" Aio0 > /dev/null
        bdev=Aio0
    else
        rpc bdev_null_create Null0 4096 512 > /dev/null # MB, block size
    fi
    if ((delay_us > 0)); then
        rpc bdev_delay_create -b "$bdev" -d Delay0 -r "$delay_us" -t "$delay_us" \
            -w "$delay_us" -n "$delay_us" > /dev/null
        bdev=Delay0
    fi
    rpc nvmf_create_subsystem "$NQN" -a -s NTPROF0000000001 -d ntprof > /dev/null
    rpc nvmf_subsystem_add_ns "$NQN" "$bdev" > /dev/null
    rpc nvmf_subsystem_add_listener "$NQN" -t "$TRTYPE" -a "$ADDR" -s "$SVC" -f ipv4 > /dev/null
    trid
}

down() {
    if running; then
        local pid
        pid=$(cat "$PIDFILE")
        kill -INT "$pid"
        for _ in $(seq 100); do kill -0 "$pid" 2> /dev/null || break; sleep 0.1; done
    fi
    rm -f "$PIDFILE" "$SOCK"
}

case "$ACTION" in
    up) up "$@" ;;
    down) down ;;
    trid) trid ;;
    *) echo "usage: $0 up [delay_us] | down | trid" >&2; exit 2 ;;
esac
