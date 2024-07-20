// #include <linux/blkdev.h>
#include "k_nvme_tcp_layer.h"

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/nvme.h>
#include <linux/proc_fs.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "k_ntm.h"
#include "util.h"

static atomic64_t sample_cnt;

// struct mutex current_io_lock;
spinlock_t current_io_lock;
struct nvme_tcp_io_instance *current_io;

struct proc_dir_entry *entry_nvme_tcp_dir;
struct proc_dir_entry *entry_nvme_tcp_stat;
static char *dir_name = "nvme_tcp";
static char *stat_name = "stat";

struct shared_nvme_tcp_layer_stat *shared_nvme_tcp_stat;
struct atomic_nvme_tcp_stat *a_nvme_tcp_stat;

static bool to_sample(void) {
  return atomic64_inc_return(&sample_cnt) % args->rate == 0;
}

void append_event(struct nvme_tcp_io_instance *inst, u64 ts,
                  enum nvme_tcp_trpt trpt, int size_info, u64 ts2) {
  if (inst->cnt >= EVENT_NUM) {
    inst->is_spoiled = true;
    pr_info("too many events for one instance\n");
    return;
  }
  inst->ts[inst->cnt] = ts;
  inst->trpt[inst->cnt] = trpt;
  inst->ts2[inst->cnt] = ts2;
  inst->sizs[inst->cnt] = size_info;
  inst->cnt++;
}

/**
 * This function is called when the nvme_tcp_queue_rq tracepoint is triggered
 * @param ignore: the first parameter of the tracepoint
 * @param req: the request
 */
void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int qid,
                          bool *to_trace, int len1, int len2,
                          long long unsigned int time) {
  // u32 qid;
  struct bio *b;

  if (!ctrl || args->io_type + rq_data_dir(req) == 1) return;

  if (req->rq_disk && req->rq_disk->disk_name) {
    if (!is_same_dev_name(req->rq_disk->disk_name, args->dev)) return;
  } else {
    return;
  }

  /** get queue id */
  // qid = (!req->q->queuedata) ? 0 : req->mq_hctx->queue_num + 1;

  /** ignore the request from the queue 0 (admin queue) */
  if (qid == 0) return;
  // pr_info("%d, %d, %llu;\n", req->tag, 0, ktime_get_real_ns());

  if (to_sample()) {
    spin_lock_bh(&current_io_lock);
    if (current_io == NULL) {
      u64 lat = 0;
      u32 size = 0;

      b = req->bio;
      while (b) {
        if (!lat) lat = __bio_issue_time(time) - bio_issue_time(&b->bi_issue);
        size += b->bi_iter.bi_size;
        b = b->bi_next;
      }
      current_io = kmalloc(sizeof(struct nvme_tcp_io_instance), GFP_KERNEL);
      init_nvme_tcp_io_instance(current_io, (rq_data_dir(req) == WRITE),
                                req->tag, len1 + len2, 0, false, false, false,
                                size);
      current_io->before = lat;
      append_event(current_io, time, QUEUE_RQ, 0, 0);
      *to_trace = true;
      current_io->qid = qid;
    }
    spin_unlock_bh(&current_io_lock);
  }
}

void nvme_tcp_stat_update(u64 now) {
  copy_nvme_tcp_stat(a_nvme_tcp_stat, &shared_nvme_tcp_stat->all_time_stat);
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int qid,
                               int cmdid, bool is_initial,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, QUEUE_REQUEST, 0, 0);
    current_io->cmdid = cmdid;
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_try_send(void *ignore, struct request *req, int qid,
                          long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND, 0, 0);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int qid,
                                  int len, int local_port,
                                  long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (args->qid[qid] && qid2port[qid] == -1) {
    qid2port[qid] = local_port;
    pr_info("set qid %d to port %d\n", qid, local_port);
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_CMD_PDU, 0, 0);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int qid,
                                   int len, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_DATA_PDU, 0, 0);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int qid,
                               int len, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_DATA, len, 0);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req, int qid,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, DONE_SEND_REQ, 0, 0);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_try_recv(void *ignore, int offset, size_t len, int recv_stat,
                          int qid, unsigned long long time,
                          long long recv_time) {
  // if (req->tag == current_io->req_tag) {
  // append_event(current_io, time, TRY_RECV);
  // }
  // pr_info("on_nvme_tcp_try_recv\n");
  // if (!ctrl) {
  //   return;
  // }
  // if (current_io && current_io->contains_c2h) {
  //   /* TODO: to remove this test */
  //   if (current_io->is_write) {
  //     pr_err("write io contains c2h data\n");
  //   }
  //   // current_io->sizs[current_io->cnt] = len;
  //   append_event(current_io, time, TRY_RECV, len, recv_time);
  // }
}

// void on_nvme_tcp_recv_pdu(void *ignore, int consumed, unsigned char pdu_type,
//                           int qid, unsigned long long time) {
//   return;
// }

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq, int qid,
                                 int data_remain, unsigned long long time,
                                 long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && rq->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, HANDLE_C2H_DATA, data_remain, recv_time);
    current_io->contains_c2h = true;
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int qid,
                           int cp_len, unsigned long long time,
                           long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && rq->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, RECV_DATA, cp_len, recv_time);
  }
  spin_unlock_bh(&current_io_lock);
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req, int qid,
                            unsigned long long time, long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    current_io->contains_r2t = true;
    append_event(current_io, time, HANDLE_R2T, 0, recv_time);
  }
  spin_unlock_bh(&current_io_lock);
}

bool is_valid_read(struct nvme_tcp_io_instance *io) {
  int ret;
  int i;

  if (io->cnt < 8) {
    return false;
  }
  ret = true;
  ret = ret && io->trpt[0] == QUEUE_RQ;
  ret = ret && io->trpt[1] == QUEUE_REQUEST;
  ret = ret && io->trpt[2] == TRY_SEND;
  ret = ret && io->trpt[3] == TRY_SEND_CMD_PDU;
  ret = ret && io->trpt[4] == DONE_SEND_REQ;
  ret = ret && io->trpt[5] == HANDLE_C2H_DATA;
  for (i = 6; i < io->cnt - 1; i++) {
    ret = ret && io->trpt[i] == RECV_DATA;
  }
  ret = ret && io->trpt[io->cnt - 1] == PROCESS_NVME_CQE;
  return ret;
}

void update_atomic_read_breakdown(struct nvme_tcp_io_instance *io,
                                  struct atomic_nvme_tcp_read_breakdown *rb) {
  if (!is_valid_read(io)) {
    pr_err("event sequence is not correct\n");
    print_io_instance(io);
    return;
  }
  atomic64_inc(&rb->cnt);
  atomic64_add(io->ts[2] - io->ts[1], &rb->sub_q);
  atomic64_add(io->ts[3] - io->ts[2], &rb->req_proc);
  atomic64_add(io->ts2[5] - io->ts[3], &rb->waiting);
  atomic64_add(io->ts[5] - io->ts2[5], &rb->comp_q);
  atomic64_add(io->ts[io->cnt - 1] - io->ts[5], &rb->resp_proc);
  atomic64_add(io->ts[io->cnt - 1] - io->ts[1], &rb->e2e);
}

bool is_valid_write(struct nvme_tcp_io_instance *io, bool contains_r2t) {
  if (contains_r2t) {
    int ret;
    int i;

    if (io->cnt < 11) {
      return false;
    }
    ret = true;
    ret = ret && io->trpt[0] == QUEUE_RQ;
    ret = ret && io->trpt[1] == QUEUE_REQUEST;
    ret = ret && io->trpt[2] == TRY_SEND;
    ret = ret && io->trpt[3] == TRY_SEND_CMD_PDU;
    ret = ret && io->trpt[4] == DONE_SEND_REQ;
    ret = ret && io->trpt[5] == HANDLE_R2T;
    ret = ret && io->trpt[6] == QUEUE_REQUEST;
    ret = ret && io->trpt[7] == TRY_SEND;
    ret = ret && io->trpt[8] == TRY_SEND_DATA_PDU;

    for (i = 9; i < io->cnt - 2; i++) {
      ret = ret && io->trpt[i] == TRY_SEND_DATA;
    }
    ret = ret && io->trpt[io->cnt - 2] == DONE_SEND_REQ;
    ret = ret && io->trpt[io->cnt - 1] == PROCESS_NVME_CQE;
    return ret;
  } else {
    int ret;
    int i;

    if (io->cnt < 6) {
      return false;
    }
    ret = true;
    ret = ret && io->trpt[0] == QUEUE_RQ;
    ret = ret && io->trpt[1] == QUEUE_REQUEST;
    ret = ret && io->trpt[2] == TRY_SEND;
    ret = ret && io->trpt[3] == TRY_SEND_CMD_PDU;

    for (i = 4; i < io->cnt - 2; i++) {
      ret = ret && io->trpt[i] == TRY_SEND_DATA;
    }
    ret = ret && io->trpt[io->cnt - 2] == DONE_SEND_REQ;
    ret = ret && io->trpt[io->cnt - 1] == PROCESS_NVME_CQE;
    return ret;
  }
}

void update_atomic_write_breakdown(struct nvme_tcp_io_instance *io,
                                   struct atomic_nvme_tcp_write_breakdown *wb) {
  if (io->contains_r2t) {
    if (!is_valid_write(io, true)) {
      pr_err("event sequence is not correct\n");
      print_io_instance(io);
      return;
    }
    atomic64_add(io->ts[2] - io->ts[1], &wb->sub_q1);
    atomic64_add(io->ts[3] - io->ts[2], &wb->req_proc1);
    atomic64_add(io->ts2[5] - io->ts[3], &wb->waiting1);
    atomic64_add(io->ts[5] - io->ts2[5], &wb->ready_q);
    atomic64_add(io->ts[7] - io->ts[5], &wb->sub_q2);
    atomic64_add(io->ts[io->cnt - 3] - io->ts[7], &wb->req_proc2);
    atomic64_add(io->ts2[io->cnt - 1] - io->ts[io->cnt - 3], &wb->waiting2);
    atomic64_add(io->ts[io->cnt - 1] - io->ts2[io->cnt - 1], &wb->comp_q);
  } else {
    if (!is_valid_write(io, false)) {
      pr_err("event sequence is not correct\n");
      print_io_instance(io);
      return;
    }
    atomic64_add(io->ts[3] - io->ts[2], &wb->sub_q1);
    atomic64_add(io->ts[io->cnt - 3] - io->ts[2], &wb->req_proc1);
    atomic64_add(io->ts2[io->cnt - 1] - io->ts[io->cnt - 3], &wb->waiting1);
    atomic64_add(io->ts[io->cnt - 1] - io->ts2[io->cnt - 1], &wb->comp_q);
  }
  atomic64_add(io->ts[io->cnt - 1] - io->ts[1], &wb->e2e);
  atomic64_inc(&wb->cnt);
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req, int qid,
                                  unsigned long long time,
                                  long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (qid == 0) return;
  // pr_info("%d, %d, %llu;\n", req->tag, 2, recv_time);  // recv time
  // pr_info("%d, %d, %llu;\n", req->tag, 1, time);

  spin_lock_bh(&current_io_lock);
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, PROCESS_NVME_CQE, 0, recv_time);

    if (args->detail) print_io_instance(current_io);

    /** update the all-time statistic */
    if (rq_data_dir(req)) {
      /** get the size of the request */
      int size = current_io->size;
      int idx = size_to_enum(size);
      update_atomic_write_breakdown(current_io, &a_nvme_tcp_stat->write[idx]);
      atomic64_add(current_io->before, &a_nvme_tcp_stat->write_before[idx]);
    } else {
      int size = current_io->size;
      int idx = size_to_enum(size);
      update_atomic_read_breakdown(current_io, &a_nvme_tcp_stat->read[idx]);
      atomic64_add(current_io->before, &a_nvme_tcp_stat->read_before[idx]);
    }
    current_io = NULL;
  }
  spin_unlock_bh(&current_io_lock);
}

void on_recv_msg_types(void *ignore, int *cnt, int qid, u64 ts) {
  // int i;
  // pr_info(
  //     "qid: %d, nvme_tcp_rsp: %d, nvme_tcp_rsp: %d, nvme_tcp_r2t: %d, ts:
  //     %llu", qid, cnt[5], cnt[7], cnt[9], ts);
}

static int nvmetcp_register_tracepoint(void) {
  int ret;
  if ((ret = register_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL)))
    goto failed;
  if ((ret = register_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request,
                                                   NULL)))
    goto unresigter_queue_rq;
  if ((ret = register_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL)))
    goto unregister_queue_request;
  if ((ret = register_trace_nvme_tcp_try_send_cmd_pdu(
           on_nvme_tcp_try_send_cmd_pdu, NULL)))
    goto unregister_try_send;
  if ((ret = register_trace_nvme_tcp_try_send_data_pdu(
           on_nvme_tcp_try_send_data_pdu, NULL)))
    goto unregister_try_send_cmd_pdu;
  if ((ret = register_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data,
                                                   NULL)))
    goto unregister_try_send_data_pdu;
  if ((ret = register_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req,
                                                   NULL)))
    goto unregister_try_send_data;
  if ((ret = register_trace_nvme_tcp_try_recv(on_nvme_tcp_try_recv, NULL)))
    goto unregister_done_send_req;
  if ((ret = register_trace_nvme_tcp_handle_c2h_data(
           on_nvme_tcp_handle_c2h_data, NULL)))
    goto unregister_try_recv;
  if ((ret = register_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL)))
    goto unregister_handle_c2h_data;
  if ((ret = register_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL)))
    goto unregister_recv_data;
  if ((ret = register_trace_nvme_tcp_process_nvme_cqe(
           on_nvme_tcp_process_nvme_cqe, NULL)))
    goto unregister_handle_r2t;
  // if((ret = register_trace_recv_msg_types(on_recv_msg_types, NULL)))
  //   goto unregister_process_nvme_cqe;
  return 0;

unregister_process_nvme_cqe:
  unregister_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe,
                                             NULL);
unregister_handle_r2t:
  unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
unregister_recv_data:
  unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
unregister_handle_c2h_data:
  unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
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
  unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
  unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
  unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
  unregister_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe,
                                             NULL);
  // unregister_trace_recv_msg_types(on_recv_msg_types, NULL);
}

static int mmap_nvme_tcp_stat(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(shared_nvme_tcp_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_nvme_tcp_stat_fops = {
    .proc_mmap = mmap_nvme_tcp_stat,
};

int init_nvmetcp_proc_entries(void) {
  entry_nvme_tcp_dir = proc_mkdir(dir_name, parent_dir);
  if (!entry_nvme_tcp_dir) return -ENOMEM;

  entry_nvme_tcp_stat =
      proc_create(stat_name, 0, entry_nvme_tcp_dir, &ntm_nvme_tcp_stat_fops);
  if (!entry_nvme_tcp_stat) return -ENOMEM;
  return 0;
}

static void remove_nvmetcp_proc_entries(void) {
  remove_proc_entry(stat_name, entry_nvme_tcp_dir);
  remove_proc_entry(dir_name, parent_dir);
}

int init_nvmetcp_variables(void) {
  current_io = NULL;
  spin_lock_init(&current_io_lock);
  // mutex_init(&current_io_lock);

  atomic64_set(&sample_cnt, 0);

  a_nvme_tcp_stat = kmalloc(sizeof(*a_nvme_tcp_stat), GFP_KERNEL);
  if (!a_nvme_tcp_stat) return -ENOMEM;
  init_atomic_nvme_tcp_stat(a_nvme_tcp_stat);

  shared_nvme_tcp_stat = vmalloc(sizeof(*shared_nvme_tcp_stat));
  if (!shared_nvme_tcp_stat) return -ENOMEM;
  init_shared_nvme_tcp_layer_stat(shared_nvme_tcp_stat);

  return 0;
}

int clear_nvmetcp_variables(void) {
  if (a_nvme_tcp_stat) {
    kfree(a_nvme_tcp_stat);
  }
  if (shared_nvme_tcp_stat) {
    vfree(shared_nvme_tcp_stat);
  }
  return 0;
}

int init_nvme_tcp_layer_monitor(void) {
  int ret;
  ret = init_nvmetcp_variables();
  if (ret) {
    return ret;
  }
  ret = init_nvmetcp_proc_entries();
  if (ret) {
    return ret;
  }
  ret = nvmetcp_register_tracepoint();
  if (ret) {
    return ret;
  }
  pr_info("start nvmetcp module monitor\n");
  return 0;
}

void exit_nvme_tcp_layer_monitor(void) {
  nvmetcp_unregister_tracepoint();
  remove_nvmetcp_proc_entries();
  clear_nvmetcp_variables();
  pr_info("exit nvmetcp module monitor\n");
}