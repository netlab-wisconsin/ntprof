#include "trace_blk.h"
#include <trace/events/block.h>
#include "host.h"

void on_block_rq_complete(void *ignore, struct request *rq, int err, unsigned int nr_bytes) {

}


int register_blk_tracepoints(void) {
    int ret;

    // register the tracepoints
    if ((ret = register_trace_block_rq_complete(on_block_rq_complete, NULL))) {
        goto failed;
    }
    return 0;

    // rollback
failed:
    return ret;
}


void unregister_blk_tracepoints(void) {
    unregister_trace_block_rq_complete(on_block_rq_complete, NULL);
}
