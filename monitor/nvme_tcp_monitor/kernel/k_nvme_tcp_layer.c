// #include <linux/blkdev.h>
#include "k_nvme_tcp_layer.h"

#include <linux/blk-mq.h>
#include <linux/blk_types.h>
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
struct atomic_nvmetcp_throughput *a_throughput[MAX_QID];

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

void pr_info_lock(bool grab, int core, int qid, char *msg) {
  // pr_info("core: %d, qid: %d, %s, grab: %d\n", core, qid, msg, grab);
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

  // pr_info("queue_rq is called, qid: %d\n", qid);

  /** if monitoring is not started, or if the io type does not match, do not
   * record the I/O */
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) return;

  /** if the disk name is different, do not record the I/O */
  if (!req->rq_disk || !req->rq_disk->disk_name ||
      !is_same_dev_name(req->rq_disk->disk_name, args->dev))
    return;

  /**
   * record throughput
   * 1. if the time gap is greater than 10 second, update the first ts to
   * current timestamp, and the last timestamp to 0
   * 2. increase read/write cnt accordingly
   */
  s64 gap = time - atomic64_read(&a_throughput[qid]->last_ts);
  if (gap > 10 * NSEC_PER_SEC || -gap > 10 * NSEC_PER_SEC) {
    pr_info(
        "update first timestamp, since the time gap is too large %lld, bool: "
        "%d\n",
        time - atomic64_read(&a_throughput[qid]->last_ts),
        time - atomic64_read(&a_throughput[qid]->last_ts) > 10 * NSEC_PER_SEC);
    atomic64_set(&a_throughput[qid]->first_ts, time);
    atomic64_set(&a_throughput[qid]->last_ts, time);
  } else {
    atomic64_set(&a_throughput[qid]->last_ts, time);
  }

  /** ignore the request from the queue 0 (admin queue) */
  if (qid == 0) return;

  b = req->bio;
  u64 lat = 0;
  u32 size = 0;

  if (b == NULL && req_op(req) != REQ_OP_FLUSH) {
    pr_err("bio is NULL, flag is %u, opresult is %u\n", req->cmd_flags,
           req_op(req));
  }

  // traverse the bio and update throughput info
  while (b) {
    // if (rq_data_dir(req)) {
    //   atomic64_inc(
    //       &a_throughput[qid]->write_cnt[size_to_enum(b->bi_iter.bi_size)]);
    // } else {
    //   atomic64_inc(
    //       &a_throughput[qid]->read_cnt[size_to_enum(b->bi_iter.bi_size)]);
    // }
    // if (!lat) lat = __bio_issue_time(time) - bio_issue_time(&b->bi_issue);
    size += b->bi_iter.bi_size;
    b = b->bi_next;
  }

  lat = time - req->start_time_ns;

  // if to_sample, update the current_io
  if (to_sample()) {
    spin_lock_bh(&current_io_lock);
    pr_info_lock(true, smp_processor_id(), qid, "queue_rq");
    if (current_io == NULL) {
      current_io = kmalloc(sizeof(struct nvme_tcp_io_instance), GFP_KERNEL);
      init_nvme_tcp_io_instance(current_io, req_op(req), req->tag, len1 + len2,
                                0, false, false, false, size);
      current_io->before = lat;
      append_event(current_io, time, QUEUE_RQ, 0, 0);
      *to_trace = true;
      current_io->qid = qid;
    }
    spin_unlock_bh(&current_io_lock);
    pr_info_lock(false, smp_processor_id(), qid, "queue_rq");
  }
  // *to_trace = true;
}

void nvme_tcp_stat_update(u64 now) {
  copy_nvme_tcp_stat(a_nvme_tcp_stat, &shared_nvme_tcp_stat->all_time_stat);
  int i;
  for (i = 0; i < MAX_QID; i++)
    copy_nvmetcp_throughput(a_throughput[i], &shared_nvme_tcp_stat->tp[i]);
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int qid,
                               int cmdid, bool is_initial,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "queue_request");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, QUEUE_REQUEST, 0, 0);
    current_io->cmdid = cmdid;
    // pr_info("cmd_id: %d, qid: %d, req_tag: %d, size: %d\n",
    // current_io->cmdid, qid, current_io->req_tag, current_io->size);
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "queue_request");
}

void on_nvme_tcp_try_send(void *ignore, struct request *req, int qid,
                          long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  // pr_info(">>>> try_send is called, cmdid: %u, qid: %d\n", cmdid, qid);

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "try_send");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    if (current_io->trpt[current_io->cnt - 1] != QUEUE_REQUEST) {
      pr_err("try_send is not after queue_request, current commd id is %d\n",
             current_io->req_tag);
    }
    append_event(current_io, time, TRY_SEND, 0, 0);
  } else {
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "try_send");
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

  // pr_info("try_send_cmd_pdu, len: %d\n", len);

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "try_send_cmd_pdu");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_CMD_PDU, 0, 0);
    current_io->send_size[0] += len;
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "try_send_cmd_pdu");
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int qid,
                                   int len, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "try_send_data_pdu");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_DATA_PDU, 0, 0);
    current_io->send_size[1] += len;
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "try_send_data_pdu");
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int qid,
                               int len, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "try_send_data");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, TRY_SEND_DATA, len, 0);
    if (len > 0) {
      if (current_io->contains_r2t) {
        current_io->send_size[1] += len;
      } else {
        current_io->send_size[0] += len;
      }
    }
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "try_send_data");
}

// unsigned int estimate_latency(int size, int cwnd, int mtu, int rtt) {
//   /** calculate the following value
//    *
//    * number of packets = ceiling (size / mtu)
//    * round trip number  = ceiling (number of packets / cwnd)
//    * tramsmission time = round trip number * rtt
//    */
//   if (cwnd == 0) {
//     pr_info("cwnd is 0\n");
//     return 0;
//   }
//   int num_packets = (size + mtu - 1) / mtu;
//   int round_trip_num = (num_packets + cwnd - 1) / cwnd;
//   return round_trip_num * rtt;
// }

void on_nvme_tcp_done_send_req(void *ignore, struct request *req, int qid,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "done_send_req");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, DONE_SEND_REQ, 0, 0);

    switch (req_op(req)) {
      case REQ_OP_READ:
        // if it is read request, update the timestamp in the first slot
        // current_io->estimated_transmission_time[0] = estimate_latency(
        //     current_io->send_size[0], cwnds[qid], args->mtu, args->rtt);
        current_io->estimated_transmission_time[0] = 0;
        break;
      case REQ_OP_WRITE:
        // if it is write request, check if it is
        if (current_io->contains_r2t) {
          // update timestamp in the second slot
          // current_io->estimated_transmission_time[1] = estimate_latency(
          //     current_io->send_size[1], cwnds[qid], args->mtu, args->rtt);
          current_io->estimated_transmission_time[1] = 0;
        } else {
          // update the timestamp in the first slot
          // current_io->estimated_transmission_time[0] = estimate_latency(
          //     current_io->send_size[0], cwnds[qid], args->mtu, args->rtt);
          current_io->estimated_transmission_time[0] = 0;
        }
        break;
      default:
        break;
    }
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "done_send_req");
}

void on_nvme_tcp_try_recv(void *ignore, int offset, size_t len, int recv_stat,
                          int qid, unsigned long long time,
                          long long recv_time) {}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq, int qid,
                                 int data_remain, unsigned long long time,
                                 long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "handle_c2h_data");
  if (current_io && rq->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, HANDLE_C2H_DATA, data_remain, recv_time);
    current_io->contains_c2h = true;
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "handle_c2h_data");
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int qid,
                           int cp_len, unsigned long long time,
                           long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "recv_data");
  if (current_io && rq->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, RECV_DATA, cp_len, recv_time);
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "recv_data");
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req, int qid,
                            unsigned long long time, long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "handle_r2t");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    current_io->contains_r2t = true;
    append_event(current_io, time, HANDLE_R2T, 0, recv_time);
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "handle_r2t");
}

bool is_valid_flush(struct nvme_tcp_io_instance *io) {
  if (io->cnt != 6) return false;
  bool ret = true;
  ret = ret && io->trpt[0] == QUEUE_RQ;
  ret = ret && io->trpt[1] == QUEUE_REQUEST;
  ret = ret && io->trpt[2] == TRY_SEND;
  ret = ret && io->trpt[3] == TRY_SEND_CMD_PDU;
  ret = ret && io->trpt[4] == DONE_SEND_REQ;
  ret = ret && io->trpt[5] == PROCESS_NVME_CQE;
  return ret;
}

void update_atomic_flush_breakdown(struct nvme_tcp_io_instance *io,
                                   struct atomic_nvme_tcp_flush_breakdown *fb) {
  if (!is_valid_flush(io)) {
    pr_err("event sequence is not correct for flush\n");
    print_io_instance(io);
    return;
  }
  atomic64_inc(&fb->cnt);
  atomic64_add(io->ts[2] - io->ts[1], &fb->sub_q);
  atomic64_add(io->ts[3] - io->ts[2], &fb->req_proc);
  atomic64_add(io->ts2[5] - io->ts[3], &fb->waiting);
  atomic64_add(io->ts[5] - io->ts2[5], &fb->comp_q);
  atomic64_add(io->ts[5] - io->ts[1], &fb->e2e);
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
    pr_err("event sequence is not correct for read\n");
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
  atomic64_add(io->estimated_transmission_time[0], &rb->trans);
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
      // if current event is TRY_SEND, the last event should be TRY_SEND_DATA
      // with a failed ret otherwise, current event should be TRY_SEND_DATA
      ret = ret && ((io->trpt[i] == TRY_SEND &&
                     io->trpt[i - 1] == TRY_SEND_DATA && io->sizs[i - 1] < 0) ||
                    io->trpt[i] == TRY_SEND_DATA);
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
      ret = ret && ((io->trpt[i] == TRY_SEND &&
                     io->trpt[i - 1] == TRY_SEND_DATA && io->sizs[i - 1] < 0) ||
                    io->trpt[i] == TRY_SEND_DATA);
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
      pr_err("event sequence is not correct for write\n");
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

    atomic64_add(io->estimated_transmission_time[0], &wb->trans1);
    atomic64_add(io->estimated_transmission_time[1], &wb->trans2);
    atomic64_inc(&wb->cnt2);
  } else {
    if (!is_valid_write(io, false)) {
      pr_err("event sequence is not correct for write\n");
      print_io_instance(io);
      return;
    }
    atomic64_add(io->ts[2] - io->ts[1], &wb->sub_q1);
    atomic64_add(io->ts[io->cnt - 3] - io->ts[2], &wb->req_proc1);
    atomic64_add(io->ts2[io->cnt - 1] - io->ts[io->cnt - 3], &wb->waiting1);
    atomic64_add(io->ts[io->cnt - 1] - io->ts2[io->cnt - 1], &wb->comp_q);
    atomic64_add(io->estimated_transmission_time[0], &wb->trans1);
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
  // pr_info("%d, %d, %llu, %d;\n", req->tag, 1, time, rq_data_dir(req));
  spin_lock_bh(&current_io_lock);
  pr_info_lock(true, smp_processor_id(), qid, "process_nvme_cqe");
  if (current_io && req->tag == current_io->req_tag && qid == current_io->qid) {
    append_event(current_io, time, PROCESS_NVME_CQE, 0, recv_time);

    if (args->detail) print_io_instance(current_io);

    int size, idx;
    switch (req_op(req)) {
      case REQ_OP_FLUSH:
        update_atomic_flush_breakdown(current_io, &a_nvme_tcp_stat->flush);
        atomic64_add(current_io->before, &a_nvme_tcp_stat->flush_before);
        break;
      case REQ_OP_WRITE:
        /** get the size of the request */
        size = current_io->size;
        idx = write_size_to_enum(size, current_io->contains_r2t);
        update_atomic_write_breakdown(current_io, &a_nvme_tcp_stat->write[idx]);
        atomic64_add(current_io->before, &a_nvme_tcp_stat->write_before[idx]);
        break;
      case REQ_OP_READ:
        size = current_io->size;
        idx = read_size_to_enum(size);
        update_atomic_read_breakdown(current_io, &a_nvme_tcp_stat->read[idx]);
        atomic64_add(current_io->before, &a_nvme_tcp_stat->read_before[idx]);
        break;
      default:
        pr_err("unknown request type\n");
        break;
    }

    inc_req_hist(a_nvme_tcp_stat, current_io->size, req_op(req));
    kfree(current_io);
    current_io = NULL;
  }
  spin_unlock_bh(&current_io_lock);
  pr_info_lock(false, smp_processor_id(), qid, "process_nvme_cqe");
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
  return 0;

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
}

static int mmap_nvme_tcp_stat(struct file *filp, struct vm_area_struct *vma) {
  unsigned long pfn = virt_to_phys((void *)shared_nvme_tcp_stat) >> PAGE_SHIFT;

  if (remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end - vma->vm_start,
                      vma->vm_page_prot))
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

  int i;
  for (i = 0; i < MAX_QID; i++) {
    a_throughput[i] = kmalloc(sizeof(*a_throughput[i]), GFP_KERNEL);
    if (!a_throughput[i]) return -ENOMEM;
    init_atomic_nvmetcp_throughput(a_throughput[i]);
  }

  int shared_nvme_tcp_stat_size =
      PAGE_ALIGN(sizeof(struct shared_nvme_tcp_layer_stat));
  shared_nvme_tcp_stat = kmalloc(shared_nvme_tcp_stat_size, GFP_KERNEL);
  // shared_nvme_tcp_stat = vmalloc(shared_nvme_tcp_stat_size);
  // shared_nvme_tcp_stat = vmalloc(sizeof(*shared_nvme_tcp_stat));
  if (!shared_nvme_tcp_stat) return -ENOMEM;
  init_shared_nvme_tcp_layer_stat(shared_nvme_tcp_stat);

  return 0;
}

int clear_nvmetcp_variables(void) {
  if (a_nvme_tcp_stat) {
    kfree(a_nvme_tcp_stat);
  }
  int i;
  for (i = 0; i < MAX_QID; i++) {
    if (a_throughput[i]) {
      kfree(a_throughput[i]);
    }
  }
  if (shared_nvme_tcp_stat) {
    kfree(shared_nvme_tcp_stat);
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