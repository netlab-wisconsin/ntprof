#ifndef _NVMETCP_TR_H
#define _NVMETCP_TR_H

#ifdef __KERNEL__
#include <linux/atomic.h>
#else
#include <stdatomic.h>
#endif

struct nvmetcp_tr {
#ifdef __KERNEL__
    atomic64_t io_request_count;
#else
    atomic_ullong io_request_count;
#endif
};

#endif // _NVMETCP_TR_H
