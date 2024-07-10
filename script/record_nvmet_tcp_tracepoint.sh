#!/bin/bash

# 获取所有与 nvme_tcp 相关的 tracepoints
tracepoints=$(sudo trace-cmd list | grep nvmet_tcp | awk '{print $1}')

# 如果没有找到任何 tracepoint，退出脚本
if [ -z "$tracepoints" ]; then
  echo "No nvme_tcp tracepoints found."
  exit 1
fi

# 构建 trace-cmd 命令
cmd="sudo trace-cmd record"
for tp in $tracepoints; do
  cmd="$cmd -e $tp"
done

# 运行 trace-cmd 命令
echo "Running: $cmd"
eval $cmd

