#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "ntm_com.h"
#include "util.h"

#define EVENT_NUM 32

enum nvme_tcp_trpt {
  QUEUE_RQ,
  QUEUE_REQUEST,
  TRY_SEND,
  TRY_SEND_CMD_PDU,
  TRY_SEND_DATA_PDU,
  TRY_SEND_DATA,
  DONE_SEND_REQ,
  TRY_RECV,
  RECV_PDU,
  HANDLE_C2H_DATA,
  RECV_DATA,
  HANDLE_R2T,
  PROCESS_NVME_CQE,
};

void nvme_tcp_trpt_name(enum nvme_tcp_trpt trpt, char *name) {
  switch (trpt) {
    case QUEUE_RQ:
      strcpy(name, "QUEUE_RQ");
      break;
    case QUEUE_REQUEST:
      strcpy(name, "QUEUE_REQUEST");
      break;
    case TRY_SEND:
      strcpy(name, "TRY_SEND");
      break;
    case TRY_SEND_CMD_PDU:
      strcpy(name, "TRY_SEND_CMD_PDU");
      break;
    case TRY_SEND_DATA_PDU:
      strcpy(name, "TRY_SEND_DATA_PDU");
      break;
    case TRY_SEND_DATA:
      strcpy(name, "TRY_SEND_DATA");
      break;
    case DONE_SEND_REQ:
      strcpy(name, "DONE_SEND_REQ");
      break;
    case TRY_RECV:
      strcpy(name, "TRY_RECV");
      break;
    case RECV_PDU:
      strcpy(name, "RECV_PDU");
      break;
    case HANDLE_C2H_DATA:
      strcpy(name, "HANDLE_C2H_DATA");
      break;
    case RECV_DATA:
      strcpy(name, "RECV_DATA");
      break;
    case HANDLE_R2T:
      strcpy(name, "HANDLE_R2T");
      break;
    case PROCESS_NVME_CQE:
      strcpy(name, "PROCESS_NVME_CQE");
      break;
    default:
      strcpy(name, "UNK");
      break;
  }
}

struct nvme_tcp_io_instance {
  int req_tag;
  int cmd_id;
  int qid;
  int waitlist;
  u64 ts[EVENT_NUM];
  enum nvme_tcp_trpt trpt[EVENT_NUM];
  int cnt;
};

static struct nvme_tcp_io_instance current_io;

void append_event(struct nvme_tcp_io_instance *inst, u64 ts,
                  enum nvme_tcp_trpt trpt) {
  if (inst->cnt >= EVENT_NUM) {
    pr_info("too many events for one instance\n");
    return;
  }
  inst->ts[inst->cnt] = ts;
  inst->trpt[inst->cnt] = trpt;
  inst->cnt++;
}

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
void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int len1, int len2,
                          long long unsigned int time) {
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
  // pr_info("current stat for user space: sum_blk_layer_lat: %llu, cnt:
  // %llu\n",
  //         raw_nvmetcp_stat->sum_blk_layer_lat, raw_nvmetcp_stat->cnt);
}

/**
 * define call back functions for all tracepoints in nvme_tcp kayer
 */
void on_nvme_tcp_queue_request(void *ignore, struct request *req,
                               bool is_initial, long long unsigned int time) {
  // pr_info("on_nvme_tcp_queue_request\n");
}

void on_nvme_tcp_try_send(void *ignore, struct request *req,
                          long long unsigned int time) {
  // pr_info("on_nvme_tcp_try_send\n");
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req,
                                  int len, long long unsigned int time) {
  // pr_info("on_nvme_tcp_try_send_cmd_pdu\n");
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req,
                                   int len, long long unsigned int time) {
  // pr_info("on_nvme_tcp_try_send_data_pdu\n");
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req,
                               int len, long long unsigned int time) {
  // pr_info("on_nvme_tcp_try_send_data\n");
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req,
                               long long unsigned int time) {
  // pr_info("on_nvme_tcp_done_send_req\n");
}

void on_nvme_tcp_try_recv(void *ignore, int offset, size_t len, int recv_stat,
                          int qid, unsigned long long time) {
  // pr_info("on_nvme_tcp_try_recv\n");
}

void on_nvme_tcp_recv_pdu(void *ignore, int consumed, unsigned char pdu_type,
                          int qid, unsigned long long time) {
  // pr_info("on_nvme_tcp_recv_pdu\n");
}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq,
                                 int data_remain, int qid,
                                 unsigned long long time) {
  // pr_info("on_nvme_tcp_handle_c2h_data\n");
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int cp_len,
                           int qid, unsigned long long time) {
  // pr_info("on_nvme_tcp_recv_data\n");
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req,
                            unsigned long long time) {
  // pr_info("on_nvme_tcp_handle_r2t\n");
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req,
                                  unsigned long long time) {
  // pr_info("on_nvme_tcp_process_nvme_cqe\n");
}

static int nvmetcp_register_tracepoint(void) {
  // pr_info("nvmetcp_register_tracepoint\n");
  // int ret;
  // ret = register_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
  // if (ret) {
  //   pr_err("Failed to register tracepoint\n");
  //   return ret;
  // }
  // return 0;
  int ret;
  pr_info("register nvme_tcp tracepoints");
  pr_info("register nvme_tcp_queue_rq");
  if ((ret = register_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL))) {
    pr_err("Failed to register nvme_tcp_queue_rq\n");
    goto failed;
  }
  pr_info("register nvme_tcp_queue_request");
  if ((ret = register_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request,
                                                   NULL))) {
    pr_err("Failed to register nvme_tcp_queue_request\n");
    goto unresigter_queue_rq;
  }

  pr_info("register nvme_tcp_try_send");
  if ((ret = register_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL))) {
    pr_err("Failed to register nvme_tcp_try_send\n");
    goto unregister_queue_request;
  }
  pr_info("register nvme_tcp_try_send_cmd_pdu");
  if ((ret = register_trace_nvme_tcp_try_send_cmd_pdu(
           on_nvme_tcp_try_send_cmd_pdu, NULL))) {
    pr_err("Failed to register nvme_tcp_try_send_cmd_pdu\n");
    goto unregister_try_send;
  }

  pr_info("register nvme_tcp_try_send_data_pdu");
  if ((ret = register_trace_nvme_tcp_try_send_data_pdu(
           on_nvme_tcp_try_send_data_pdu, NULL))) {
    pr_err("Failed to register nvme_tcp_try_send_data_pdu\n");
    goto unregister_try_send_cmd_pdu;
  }
  pr_info("register nvme_tcp_try_send_data");
  if ((ret = register_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data,
                                                   NULL))) {
    pr_err("Failed to register nvme_tcp_try_send_data\n");
    goto unregister_try_send_data_pdu;
  }
  pr_info("register nvme_tcp_done_send_req");
  if ((ret = register_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req,
                                                   NULL))) {
    pr_err("Failed to register nvme_tcp_done_send_req\n");
    goto unregister_try_send_data;
  }
  pr_info("register nvme_tcp_try_recv");
  if ((ret = register_trace_nvme_tcp_try_recv(on_nvme_tcp_try_recv, NULL))) {
    pr_err("Failed to register nvme_tcp_try_recv\n");
    goto unregister_done_send_req;
  }
  pr_info("register nvme_tcp_recv_pdu");
  if ((ret = register_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL))) {
    pr_err("Failed to register nvme_tcp_recv_pdu\n");
    goto unregister_try_recv;
  }
  pr_info("register nvme_tcp_handle_c2h_data");
  if ((ret = register_trace_nvme_tcp_handle_c2h_data(
           on_nvme_tcp_handle_c2h_data, NULL))) {
    pr_err("Failed to register nvme_tcp_handle_c2h_data\n");
    goto unregister_recv_pdu;
  }
  pr_info("register nvme_tcp_recv_data");
  if ((ret = register_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL))) {
    pr_err("Failed to register nvme_tcp_recv_data\n");
    goto unregister_handle_c2h_data;
  }
  pr_info("register nvme_tcp_handle_r2t");
  if ((ret =
           register_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL))) {
    pr_err("Failed to register nvme_tcp_handle_r2t\n");
    goto unregister_recv_data;
  }
  pr_info("register nvme_tcp_process_nvme_cqe");
  if ((ret = register_trace_nvme_tcp_process_nvme_cqe(
           on_nvme_tcp_process_nvme_cqe, NULL))) {
    pr_err("Failed to register nvme_tcp_process_nvme_cqe\n");
    goto unregister_handle_r2t;
  }

  return 0;

unregister_handle_r2t:
  unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
unregister_recv_data:
  unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
unregister_handle_c2h_data:
  unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
unregister_recv_pdu:
  unregister_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL);
unregister_try_recv:
  unregister_trace_nvme_tcp_try_recv(on_nvme_tcp_try_recv, NULL);
unregister_done_send_req:
  unregister_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req, NULL);
unregister_try_send_data:
  unregister_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data, NULL);
unregister_try_send_data_pdu:
  unregister_trace_nvme_tcp_try_send_data_pdu(on_nvme_tcp_try_send_data_pdu,
                                              NULL);
unregister_try_send_cmd_pdu:
  unregister_trace_nvme_tcp_try_send_cmd_pdu(on_nvme_tcp_try_send_cmd_pdu,
                                             NULL);
unregister_try_send:
  unregister_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL);
unregister_queue_request:
  unregister_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request, NULL);
unresigter_queue_rq:
  unregister_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
failed:
  return ret;
}

static void nvmetcp_unregister_tracepoint(void) {
  unregister_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
  unregister_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request, NULL);
  unregister_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL);
  unregister_trace_nvme_tcp_try_send_cmd_pdu(on_nvme_tcp_try_send_cmd_pdu,
                                             NULL);
  unregister_trace_nvme_tcp_try_send_data_pdu(on_nvme_tcp_try_send_data_pdu,
                                              NULL);
  unregister_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data, NULL);
  unregister_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req, NULL);
  unregister_trace_nvme_tcp_try_recv(on_nvme_tcp_try_recv, NULL);
  unregister_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL);
  unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
  unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
  unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
  unregister_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe,
                                             NULL);
}

static int mmap_raw_nvmetcp_stat(struct file *filp,
                                 struct vm_area_struct *vma) {
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

  entry_raw_nvmetcp_stat = proc_create(
      "ntm_raw_nvmetcp_stat", 0, entry_nvmetcp_dir, &ntm_raw_nvmetcp_stat_fops);
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