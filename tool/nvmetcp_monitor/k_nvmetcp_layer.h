#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "ntm_com.h"
#include "util.h"


struct _nvmetcp_stat {
  atomic64_t sum_blk_layer_lat;
  atomic64_t cnt;
};

void _init_nvmetcp_stat(struct _nvmetcp_stat *stat) {
  atomic64_set(&stat->sum_blk_layer_lat, 0);
  atomic64_set(&stat->cnt, 0);
}

void copy_nvmetcp_stat(struct _nvmetcp_stat *src, struct nvmetcp_stat *dst) {
  dst->sum_blk_layer_lat = atomic64_read(&src->sum_blk_layer_lat);
  dst->cnt = atomic64_read(&src->cnt);
}

/**
 * a sliding window for the block layer time
 *  - start time: the bio is issued
 *  - end time: when nvme_tcp_queue_rq is called
 * Since the end time is the time it enters the nvmetcp layer,
 * we put the sliding window here.
 */
static struct sliding_window *sw_blk_layer_time;

static struct _nvmetcp_stat *_raw_nvmetcp_stat;

static struct nvmetcp_stat *raw_nvmetcp_stat;

struct proc_dir_entry *entry_nvmetcp_dir;
struct proc_dir_entry *entry_raw_nvmetcp_stat;

/**
 * This function is called when the nvme_tcp_queue_rq tracepoint is triggered
 * @param ignore: the first parameter of the tracepoint
 * @param req: the request
 */
void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int len1, int len2, long long unsigned int time) {
  /** get queue id */
  u32 qid = (!req->q->queuedata) ? 0 : req->mq_hctx->queue_num + 1;

  /** ignore the request from the queue 0 (admin queue) */
  if (qid == 0) {
    return;
  }

  u64 now = ktime_get_ns();
  u16 cnt = 0;

  struct bio *b;
  b = req->bio;
  while (b) {
    u64 _start = bio_issue_time(&b->bi_issue);
    u64 _now = __bio_issue_time(now);
    /** the unit of lat is ns */
    u64 lat = _now - _start;
    cnt++;
    b = b->bi_next;

    atomic64_add(lat, &_raw_nvmetcp_stat->sum_blk_layer_lat);
    atomic64_inc(&_raw_nvmetcp_stat->cnt);

    if (to_sample()) {
      u64 *p_lat = kmalloc(sizeof(u64), GFP_KERNEL);
      (*p_lat) = lat;
      struct sw_node *node = kmalloc(sizeof(struct sw_node), GFP_KERNEL);
      node->data = p_lat;
      node->timestamp = now;
      add_to_sliding_window(sw_blk_layer_time, node);
    }
  }
}

/**
 * a routine to update the sliding window
 */
void nvmetcp_stat_update(u64 now) {
  copy_nvmetcp_stat(_raw_nvmetcp_stat, raw_nvmetcp_stat);
  remove_from_sliding_window(sw_blk_layer_time, now - 10 * NSEC_PER_SEC);
  // pr_info("current stat for user space: sum_blk_layer_lat: %llu, cnt: %llu\n",
  //         raw_nvmetcp_stat->sum_blk_layer_lat, raw_nvmetcp_stat->cnt);
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

static int mmap_raw_nvmetcp_stat(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(raw_nvmetcp_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}


static const struct proc_ops ntm_raw_nvmetcp_stat_fops = {
    .proc_mmap = mmap_raw_nvmetcp_stat,
};

int init_nvmetcp_proc_entries(void) {
  entry_nvmetcp_dir = proc_mkdir("nvmetcp", parent_dir);
  if (!entry_nvmetcp_dir) {
    pr_err("Failed to create proc entry nvmetcp\n");
    return -ENOMEM;
  }
  
  entry_raw_nvmetcp_stat = proc_create("ntm_raw_nvmetcp_stat", 0, entry_nvmetcp_dir,
                                       &ntm_raw_nvmetcp_stat_fops);
  if (!entry_raw_nvmetcp_stat) {
    pr_err("Failed to create proc entry raw_nvmetcp_stat\n");
    vfree(raw_nvmetcp_stat);
    return -ENOMEM;
  }
  return 0;
}

static void remove_nvmetcp_proc_entries(void) {
  remove_proc_entry("ntm_raw_nvmetcp_stat", entry_nvmetcp_dir);
  remove_proc_entry("nvmetcp", parent_dir);
}

static void unmmap_raw_nvmetcp_stat(struct file *file,
                                    struct vm_area_struct *vma) {
  pr_info("unmmap_raw_nvmetcp_stat\n");
}

int init_nvmetcp_variables(void) {
  _raw_nvmetcp_stat = vmalloc(sizeof(*_raw_nvmetcp_stat));
  if (!_raw_nvmetcp_stat) -ENOMEM;
  _init_nvmetcp_stat(_raw_nvmetcp_stat);

  raw_nvmetcp_stat = vmalloc(sizeof(*raw_nvmetcp_stat));
  if (!raw_nvmetcp_stat) -ENOMEM;
  init_nvmetcp_stat(raw_nvmetcp_stat);

  sw_blk_layer_time = vmalloc(sizeof(*sw_blk_layer_time));
  if (!sw_blk_layer_time) {
    pr_err("Failed to allocate memory for sw_blk_layer_time\n");
    return -ENOMEM;
  }
  init_sliding_window(sw_blk_layer_time);
  return 0;
}

int clear_nvmetcp_variables(void) {
  pr_info("clear_nvmetcp_variables\n");
  if (_raw_nvmetcp_stat) {
    vfree(_raw_nvmetcp_stat);
  }
  if (raw_nvmetcp_stat) {
    vfree(raw_nvmetcp_stat);
  }
  if (sw_blk_layer_time) {
    vfree(sw_blk_layer_time);
  }
  return 0;
}

int _init_ntm_nvmetcp_layer(void) {
  int ret;
  ret = init_nvmetcp_variables();
  if (ret) return ret;
  ret = init_nvmetcp_proc_entries();
  if (ret) {
    pr_info("Failed to initialize nvmetcp proc entries\n");
    clear_nvmetcp_variables();
    return ret;
  }

  ret = nvmetcp_register_tracepoint();
  if (ret) {
    pr_err("Failed to register nvmetcp tracepoint\n");
    return ret;
  }
  return 0;
}

void _exit_ntm_nvmetcp_layer(void) {
  remove_nvmetcp_proc_entries();
  clear_nvmetcp_variables();
  nvmetcp_unregister_tracepoint();
  pr_info("stop nvmetcp module monitor\n");
}

#endif  // K_NVMETCP_LAYER_H