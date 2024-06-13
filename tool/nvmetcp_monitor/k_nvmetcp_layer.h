#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "ntm_com.h"
#include "util.h"

// #include <linux/blk_types.h>

/**
 * SOME LOG EXAMPLES OF TRACE-CMD
 * 
 * 4K READ
             fio-261972  [000] ..... 31675.858926: nvme_tcp_queue_rq: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cmdid=0, slba=0, pos=0, length=4096, is_write=0, req_len=0, send_len=0
             fio-261972  [000] ..... 31675.858939: [nvme_tcp_queue_request]: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cmdid=4113, slba=0, pos=0, length=4096, is_write=0, req_len=0, send_len=0
             fio-261972  [000] ..... 31675.858966: [nvme_tcp_try_send_cmd_pdu]: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cmdid=4113, slba=0, request_length=4096, send_bytes=72, is_write=0
             fio-261972  [000] ..... 31675.858967: nvme_tcp_done_send_req: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cmdid=4113, slba=0, req_len=4096, is_write=0
    kworker/0:1H-520     [000] ..... 31675.859507: nvme_tcp_try_recv: offset=0, len=4144, recv_stat=0, qid=1
    kworker/0:1H-520     [000] ..... 31675.859513: [nvme_tcp_handle_c2h_data]: nvme4: disk=nvme4c4n1, qid=1, rtag=17, data_remain=4096, is_write=0, cmdid=4113
    kworker/0:1H-520     [000] ..... 31675.859517: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cp_len=4096, is_write=0
    kworker/0:1H-520     [000] ..... 31675.859518: [nvme_tcp_process_nvme_cqe]: nvme4: disk=nvme4c4n1, qid=1, rtag=17, cmdid=4113, slba=0, pos=0, length=4096, is_write=0
 *
 * 
 * 128K READ
 
             fio-262301  [000] ..... 32746.874173: nvme_tcp_queue_rq: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cmdid=0, slba=0, pos=0, length=131072, is_write=0, req_len=0, send_len=0
             fio-262301  [000] ..... 32746.874186: [nvme_tcp_queue_request]: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cmdid=4117, slba=0, pos=0, length=131072, is_write=0, req_len=0, send_len=0
             fio-262301  [000] ..... 32746.874212: [nvme_tcp_try_send_cmd_pdu]: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cmdid=4117, slba=0, request_length=131072, send_bytes=72, is_write=0
             fio-262301  [000] ..... 32746.874213: nvme_tcp_done_send_req: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cmdid=4117, slba=0, req_len=131072, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875003: nvme_tcp_try_recv: offset=0, len=17896, recv_stat=0, qid=1
    kworker/0:1H-520     [000] ..... 32746.875014: [nvme_tcp_handle_c2h_data]: nvme4: disk=nvme4c4n1, qid=1, rtag=21, data_remain=131072, is_write=0, cmdid=4117
    kworker/0:1H-520     [000] ..... 32746.875025: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=17872, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875029: nvme_tcp_try_recv: offset=0, len=17896, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875038: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=17896, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875039: nvme_tcp_try_recv: offset=0, len=17896, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875046: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=17896, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875239: nvme_tcp_try_recv: offset=0, len=19504, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875261: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=19504, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875266: nvme_tcp_try_recv: offset=0, len=7340, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875269: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=7340, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875271: nvme_tcp_try_recv: offset=0, len=26844, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875284: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=26844, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875286: nvme_tcp_try_recv: offset=0, len=19704, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875294: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=19704, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875296: nvme_tcp_try_recv: offset=0, len=4040, recv_stat=1, qid=1
    kworker/0:1H-520     [000] ..... 32746.875298: nvme_tcp_recv_data: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cp_len=4016, is_write=0
    kworker/0:1H-520     [000] ..... 32746.875299: [nvme_tcp_process_nvme_cqe]: nvme4: disk=nvme4c4n1, qid=1, rtag=21, cmdid=4117, slba=0, pos=0, length=131072, is_write=0

 * 
 * 4K WRITE
             fio-262114  [000] ..... 31938.310311: nvme_tcp_queue_rq: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=0, slba=0, pos=0, length=4096, is_write=1, req_len=0, send_len=0
             fio-262114  [000] ..... 31938.310318: [nvme_tcp_queue_request]: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=4115, slba=0, pos=0, length=4096, is_write=1, req_len=0, send_len=0
             fio-262114  [000] ..... 31938.310326: [nvme_tcp_try_send_cmd_pdu]: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=4115, slba=0, request_length=4096, send_bytes=72, is_write=1
             fio-262114  [000] ..... 31938.310339: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=4115, slba=0, req_len=4096, send_len=4096, is_write=1
             fio-262114  [000] ..... 31938.310339: [nvme_tcp_done_send_req]: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=4115, slba=0, req_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 31938.310729: nvme_tcp_try_recv: offset=0, len=24, recv_stat=0, qid=1
    kworker/0:1H-520     [000] ..... 31938.310735: [nvme_tcp_process_nvme_cqe]: nvme4: disk=nvme4c4n1, qid=1, rtag=19, cmdid=4115, slba=0, pos=0, length=4096, is_write=1

 * 
 * 128K WRITE
 * 
             fio-262214  [000] ..... 32236.379415: nvme_tcp_queue_rq: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=0, slba=0, pos=0, length=131072, is_write=1, req_len=0, send_len=0
             fio-262214  [000] ..... 32236.379421: [nvme_tcp_queue_request]: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=4116, slba=0, pos=0, length=131072, is_write=1, req_len=0, send_len=0
             fio-262214  [000] ..... 32236.379440: [nvme_tcp_try_send_cmd_pdu]: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=4116, slba=0, request_length=131072, send_bytes=72, is_write=1
             fio-262214  [000] ..... 32236.379440: nvme_tcp_done_send_req: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=4116, slba=0, req_len=131072, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379767: nvme_tcp_try_recv: offset=0, len=24, recv_stat=0, qid=1
    kworker/0:1H-520     [000] ..... 32236.379774: [nvme_tcp_handle_r2t]: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=4116, slba=0, pos=0, length=131072, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379774: nvme_tcp_queue_request: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, pos=0, length=131072, is_write=1, req_len=0, send_len=0
    kworker/0:1H-520     [000] ..... 32236.379786: nvme_tcp_try_send_data_pdu: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=24, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379787: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379788: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379789: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379789: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379790: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379790: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379801: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379802: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379803: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379803: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379803: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379804: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379804: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379807: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379807: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379808: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379808: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379809: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379809: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379810: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379810: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379811: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379811: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379811: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379812: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379812: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379813: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379814: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379814: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379814: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379815: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379816: nvme_tcp_try_send_data: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, send_len=4096, is_write=1
    kworker/0:1H-520     [000] ..... 32236.379816: [nvme_tcp_done_send_req]: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, req_len=131072, is_write=1
    kworker/0:1H-520     [000] ..... 32236.380829: nvme_tcp_try_recv: offset=0, len=24, recv_stat=0, qid=1
    kworker/0:1H-520     [000] ..... 32236.380838: [nvme_tcp_process_nvme_cqe]: nvme4: disk=nvme4c4n1, qid=1, rtag=20, cmdid=3, slba=0, pos=0, length=131072, is_write=1


response type:
(1) r2t
(2) rsp
(3) c2h data
*/




// enum stage{
  
// }

// struct lifetime_record {
//   /** the events recorded includes 
//    *  - req tag
//    *  - :nvme_tcp_queue_rq
//    *  - :nvme_tcp_queue_request
//    *  - :nvme_tcp_try_send_cmd_pdu
//    *  - :
//   */
// }


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
  entry_raw_nvmetcp_stat = proc_create("ntm_raw_nvmetcp_stat", 0, parent_dir,
                                       &ntm_raw_nvmetcp_stat_fops);
  if (!entry_raw_nvmetcp_stat) {
    pr_err("Failed to create proc entry raw_nvmetcp_stat\n");
    vfree(raw_nvmetcp_stat);
    return -ENOMEM;
  }
  return 0;
}

static void remove_nvmetcp_proc_entries(void) {
  remove_proc_entry("ntm_raw_nvmetcp_stat", parent_dir);
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