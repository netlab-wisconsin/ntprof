#!/usr/bin/env bash
# Kept for scripts written before loopback.sh: the NVMe/TCP loopback test bed.
#   loopback-tcp.sh up [completion_ns] [io_queues] | down | dev
exec "$(dirname "$0")/loopback.sh" tcp "$@"
