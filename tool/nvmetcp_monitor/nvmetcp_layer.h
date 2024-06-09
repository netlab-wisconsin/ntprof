#ifndef NVMETCP_LAYER_H
#define NVMETCP_LAYER_H

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "util.h"
// #include <linux/blk_types.h>

/**
 * a sliding window for the block layer time
 *  - start time: the bio is issued
 *  - end time: when nvme_tcp_queue_rq is called
 * Since the end time is the time it enters the nvmetcp layer,
 * we put the sliding window here.
 */
static struct sliding_window *sw_blk_layer_time;

/**
 * This function is called when the nvme_tcp_queue_rq tracepoint is triggered
 * @param ignore: the first parameter of the tracepoint
 * @param req: the request
*/
void on_nvme_tcp_queue_rq(void* ignore, struct request* req) {
  /** get queue id */
  u32 qid = (!req->q->queuedata) ? 0 : req->mq_hctx->queue_num + 1;

  /** ignore the request from the queue 0 (admin queue) */
  if(qid == 0) {
    return;
  }

  u64 now = ktime_get_ns();
  u16 cnt = 0;

  struct bio* b;
  b = req->bio;
  while (b) {
    u64 _start = bio_issue_time(&b->bi_issue);
    u64 _now = __bio_issue_time(now);
    /** the unit of lat is ns */
    u64 lat = _now - _start;
    cnt++;
    b = b->bi_next;
    pr_info("latency: %d\n", lat);

    if(to_sample()) {
      u64 *p_lat = kmalloc(sizeof(u64), GFP_KERNEL);
      (*p_lat) = lat;
      struct sw_node* node = kmalloc(sizeof(struct sw_node), GFP_KERNEL);
      node->data = p_lat;
      node->timestamp = now;
      add_to_sliding_window(sw_blk_layer_time, node);
    }
  }
  pr_info("request bio number: %d, queue id: %d\n", cnt, qid);
}

/**
 * a routine to update the sliding window
*/
void nvmetcp_stat_update(u64 now) {
  remove_from_sliding_window(sw_blk_layer_time, now - 10 * NSEC_PER_SEC);
}



static int nvmetcp_register_tracepoint(void) {
  pr_info("nvmetcp_register_tracepoint\n");
  int ret;
  ret = register_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
  if (ret) {
    pr_err("Failed to register tracepoint\n");
    return ret;
  }
  return 0;
}

static void nvmetcp_unregister_tracepoint(void) {
  unregister_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
}


int init_nvmetcp_variables(void) {
  sw_blk_layer_time = kmalloc(sizeof(struct sliding_window), GFP_KERNEL);
  if (!sw_blk_layer_time) {
    pr_err("Failed to allocate memory for sw_blk_layer_time\n");
    return -ENOMEM;
  }
  init_sliding_window(sw_blk_layer_time);
  return 0;
}

int clear_nvmetcp_variables(void) {
  if (sw_blk_layer_time) {
    kfree(sw_blk_layer_time);
  }
  return 0;
}

int _init_ntm_nvmetcp_layer(void) {
  int ret;
  ret = init_nvmetcp_variables();
  if(ret) return ret;
  ret = nvmetcp_register_tracepoint();
  if (ret) {
    pr_err("Failed to register nvmetcp tracepoint\n");
    return ret;
  }
  return 0;
}

void _exit_ntm_nvmetcp_layer(void) {
  clear_nvmetcp_variables();
  nvmetcp_unregister_tracepoint();
  pr_info("stop nvmetcp module monitor\n");
}

#endif  // NVMETCP_LAYER_H