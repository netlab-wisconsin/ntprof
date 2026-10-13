#!/usr/bin/env bash
# Generate BTF for ntprof from the DWARF of the NVMe-oF modules.
#
# For kernels built without CONFIG_DEBUG_INFO_BTF (no /sys/kernel/btf/vmlinux)
# whose modules still carry debug info (CONFIG_DEBUG_INFO=y). The BTF of a
# module's DWARF contains the module's own types (struct nvme_tcp_queue, ...)
# and every kernel type it uses (struct request, struct sk_buff, ...), which
# is what ntprof's CO-RE relocations need. Pass the output directory to
# `ntprof record --btf DIR`.
#
#   gen-btf.sh OUTDIR [KERNEL_RELEASE | MODULE_DIR]
#
# Without a second argument the modules of the running kernel are used; a
# directory instead holds copies of the .ko files (possibly compressed), e.g.
# from a machine without pahole. Needs pahole (dwarves) >= 1.22.
set -euo pipefail

out=${1:?usage: gen-btf.sh OUTDIR [KERNEL_RELEASE | MODULE_DIR]}
src=${2:-$(uname -r)}
modules=(nvme-tcp nvmet-tcp nvme-rdma nvmet-rdma)

command -v pahole > /dev/null || { echo "pahole not found (package dwarves)" >&2; exit 1; }
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

find_module() { # name
    if [[ -d $src ]]; then
        ls "$src/$1".ko* 2> /dev/null | head -1
    else
        modinfo -k "$src" -n "$1" 2> /dev/null
    fi
}

for m in "${modules[@]}"; do
    f=$(find_module "$m") || true
    if [[ -z $f || ! -f $f ]]; then
        echo "skip $m: module not found" >&2
        continue
    fi
    case $f in
        *.zst) zstd -q -d -c "$f" > "$tmp/$m.ko" ;;
        *.xz) xz -d -c "$f" > "$tmp/$m.ko" ;;
        *.gz) gzip -d -c "$f" > "$tmp/$m.ko" ;;
        *) cp "$f" "$tmp/$m.ko" ;;
    esac
    if ! readelf -S "$tmp/$m.ko" | grep -q '\.debug_info'; then
        echo "skip $m: no DWARF in $f" >&2
        continue
    fi
    # the BTF file is named after the module as the kernel names it (nvme_tcp)
    pahole --btf_encode_detached="$out/${m//-/_}.btf" "$tmp/$m.ko"
    echo "$out/${m//-/_}.btf"
done
