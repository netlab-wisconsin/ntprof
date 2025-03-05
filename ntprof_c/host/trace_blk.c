#include "trace_blk.h"

#include <trace/events/block.h>
#include "host.h"
#include "host_logging.h"
#include "host.h"

#include "analyze.h"
#include "analyzer.h"
#include <linux/delay.h>

// #include "../target/target.h"

void on_block_rq_complete(void* ignore, struct request* rq, int err,
                          unsigned int nr_bytes) {
  if (atomic_read(&trace_on) == 0) return;

  if (!match_config(rq, &global_config)) {
    return;
  }

  struct profile_record* rec;
  int i = 0;

  for (i = 0; i < MAX_CORE_NUM; i++) {
    LOCKQ(i);
    rec = get_profile_record(&stat[i], rq);

    if (rec != NULL) {
      unsigned long long now = ktime_get_real_ns();
      append_event(rec, now, BLK_RQ_COMPLETE);
      complete_record(&stat[i], rec);
      // print_profile_record(rec);
      UNLOCKQ(i);
      break;
    }
    UNLOCKQ(i);
  }
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