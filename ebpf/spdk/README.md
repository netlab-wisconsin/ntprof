# ntprof probes for SPDK

`ntprof-usdt-v26.05.patch` adds ntprof's USDT probes (provider `ntprof`) to
SPDK v26.05:

| File | Change |
|---|---|
| `include/spdk_internal/ntprof.h` | new: `struct ntprof_times`, the sampling hash, `ntprof_sampled()`, the probe macro |
| `include/spdk/sock.h` | `spdk_sock_request.internal`: `ntprof` (mark) and `ntprof_sent_ns` |
| `include/spdk_internal/sock_module.h` | record when `sendmsg()` took a marked request |
| `lib/sock/sock.c`, `lib/sock/spdk_sock.map` | `g_ntprof_sample_every`, exported |
| `lib/nvme/nvme_tcp.c` | initiator times; probe `nvme_tcp_done`; `nvme_tcp_req.ntprof`, `.ntprof_times` |
| `lib/nvmf/tcp.c` | target times; probe `nvmf_tcp_done`; `spdk_nvmf_tcp_req.ntprof`, `.ntprof_times` |

```sh
cd spdk && git checkout v26.05 && git submodule update --init
git apply /path/to/ntprof/ebpf/spdk/ntprof-usdt-v26.05.patch
./configure --with-usdt && make
readelf -n build/bin/nvmf_tgt | grep -c 'Provider: ntprof'   # probe sites
```

Without `--with-usdt` nothing is ever sampled. With it, nothing happens
until ntprof sets `g_ntprof_sample_every` in a running process; then SPDK
records the stage times of the sampled commands and fires one probe per
command and side when it is done. What each probe marks and how
ntprof uses it: [../DESIGN.md](../DESIGN.md), section 8.

Upstreaming the patch would need SPDK's ABI bookkeeping (`libspdk_sock`
gains an exported symbol, and `struct spdk_sock_request` grows by 8 bytes)
and their unit tests.
