# ntprof probes for SPDK

`ntprof-usdt-v26.05.patch` adds ntprof's USDT probes (provider `ntprof`) to
SPDK v26.05:

| File | Change |
|---|---|
| `include/spdk_internal/ntprof.h` | new: probe macros, the sampling hash, `ntprof_sampled()` |
| `include/spdk/sock.h` | `spdk_sock_request.internal.ntprof` (in existing padding) |
| `include/spdk_internal/sock_module.h` | probe `sock_req_sent` when a marked request was sent |
| `lib/sock/sock.c`, `lib/sock/spdk_sock.map` | `g_ntprof_sample_every`, exported |
| `lib/nvme/nvme_tcp.c` | initiator probes; `nvme_tcp_req.ntprof` |
| `lib/nvmf/tcp.c` | target probes; `spdk_nvmf_tcp_req.ntprof` |

```sh
cd spdk && git checkout v26.05 && git submodule update --init
git apply /path/to/ntprof/ebpf/spdk/ntprof-usdt-v26.05.patch
./configure --with-usdt && make
readelf -n build/bin/nvmf_tgt | grep -c 'Provider: ntprof'   # probe sites
```

Without `--with-usdt` the patch compiles to nothing. With it, the probes cost
nothing until ntprof sets `g_ntprof_sample_every` in a running process, and
then fire only for the commands sampled. What each probe marks and how
ntprof uses it: [../DESIGN.md](../DESIGN.md), section 8.

Upstreaming the patch would need SPDK's ABI bookkeeping (`SO_MINOR` of
`libspdk_sock`, which gains an exported symbol) and their unit tests.
