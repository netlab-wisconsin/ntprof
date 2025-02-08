#include "trace_blk.h"
#include <trace/events/block.h>
#include "host.h"
#include "host_logging.h"

#include "analyze.h"
#include "analyzer.h"

void on_block_rq_complete(void *ignore, struct request *rq, int err, unsigned int nr_bytes) {
    int cid = smp_processor_id();
    // pr_info("on_block_rq_complete is called on core %d\n", cid);
    unsigned long flags;
    update_op_cnt(true);
    struct profile_record * rec = get_profile_record(&stat[cid], rq->tag);
    if (rec) {
        // u64 _start = bio_issue_time(&bio->bi_issue);
        // u64 _now = __bio_issue_time(ktime_get_ns());
        unsigned long long time = ktime_get_real_ns();
        append_event(rec, time, BLK_RQ_COMPLETE);
        complete_record(&stat[cid], rec);
    }
    update_op_cnt(false);
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
