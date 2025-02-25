#include "trace_blk.h"
#include <trace/events/block.h>
#include "host.h"
#include "host_logging.h"

#include "analyze.h"
#include "analyzer.h"
#include <linux/delay.h>

void on_block_rq_complete(void *ignore, struct request *rq, int err, unsigned int nr_bytes) {
    // if (atomic_read(&trace_on) == 0) return;
    // int cid = smp_processor_id();
    // pr_info("on_block_rq_complete is called on core %d, req->tag=%d\n", cid, rq->tag);
    // SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_block_rq_complete");
    // //update_op_cnt(true);
    // // pr_info("on_nvme_tcp_queue_rq, PID:%d, core_id:%d, tag:%d\n", current->pid, cid, rq->tag);
    // struct profile_record *rec = get_profile_record(&stat[cid], rq);
    // if (rec) {
    //     pr_info("on_block_rq_complete, req->tag=%d", rq->tag);
    //     // u64 _start = bio_issue_time(&bio->bi_issue);
    //     // u64 _now = __bio_issue_time(ktime_get_ns());
    //     unsigned long long time = ktime_get_real_ns();
    //     append_event(rec, time, BLK_RQ_COMPLETE);
    //     complete_record(&stat[cid], rec);
    //     // if (is_valid_profile_record(rec) == 0) {
    //     //     pr_err("bad profiling record");
    //     //     print_profile_record(rec);
    //     //     // msleep(2000);
    //     // }
    //     // print_profile_record(rec);
    // }
    // SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_block_rq_complete");
    // // update_op_cnt(false);

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
