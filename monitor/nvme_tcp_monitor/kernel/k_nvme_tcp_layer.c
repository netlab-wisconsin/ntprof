// #include <linux/blkdev.h>
#include "k_nvme_tcp_layer.h"

#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/nvme.h>
#include <linux/proc_fs.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "k_ntm.h"
#include "util.h"

static atomic64_t sample_cnt;

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

void init_atomic_nvme_tcp_read_breakdown(
    struct atomic_nvme_tcp_read_breakdown *rb) {
  atomic64_set(&rb->cnt, 0);
  atomic64_set(&rb->t_inqueue, 0);
  atomic64_set(&rb->t_reqcopy, 0);
  atomic64_set(&rb->t_datacopy, 0);
  atomic64_set(&rb->t_waiting, 0);
  atomic64_set(&rb->t_waitproc, 0);
  atomic64_set(&rb->t_endtoend, 0);
}

void init_atomic_nvme_tcp_write_breakdown(
    struct atomic_nvme_tcp_write_breakdown *wb) {
  atomic64_set(&wb->cnt, 0);
  atomic64_set(&wb->t_inqueue, 0);
  atomic64_set(&wb->t_reqcopy, 0);
  atomic64_set(&wb->t_datacopy, 0);
  atomic64_set(&wb->t_waiting, 0);
  atomic64_set(&wb->t_endtoend, 0);
}

void init_atomic_nvme_tcp_stat(struct atomic_nvme_tcp_stat *stat) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    init_atomic_nvme_tcp_read_breakdown(&stat->read[i]);
    init_atomic_nvme_tcp_write_breakdown(&stat->write[i]);
    atomic64_set(&stat->read_before[i], 0);
    atomic64_set(&stat->write_before[i], 0);
  }
}

void copy_nvme_tcp_write_breakdown(struct atomic_nvme_tcp_write_breakdown *src,
                                   struct nvmetcp_write_breakdown *dst) {
  dst->cnt = atomic64_read(&src->cnt);
  dst->t_inqueue = atomic64_read(&src->t_inqueue);
  dst->t_reqcopy = atomic64_read(&src->t_reqcopy);
  dst->t_datacopy = atomic64_read(&src->t_datacopy);
  dst->t_waiting = atomic64_read(&src->t_waiting);
  dst->t_endtoend = atomic64_read(&src->t_endtoend);
}

void copy_nvme_tcp_read_breakdown(struct atomic_nvme_tcp_read_breakdown *src,
                                  struct nvmetcp_read_breakdown *dst) {
  dst->cnt = atomic64_read(&src->cnt);
  dst->t_inqueue = atomic64_read(&src->t_inqueue);
  dst->t_reqcopy = atomic64_read(&src->t_reqcopy);
  dst->t_datacopy = atomic64_read(&src->t_datacopy);
  dst->t_waiting = atomic64_read(&src->t_waiting);
  dst->t_waitproc = atomic64_read(&src->t_waitproc);
  dst->t_endtoend = atomic64_read(&src->t_endtoend);
}

void copy_nvme_tcp_stat(struct atomic_nvme_tcp_stat *src,
                        struct nvme_tcp_stat *dst) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    copy_nvme_tcp_read_breakdown(&src->read[i], &dst->read[i]);
    copy_nvme_tcp_write_breakdown(&src->write[i], &dst->write[i]);
    dst->read_before[i] = atomic64_read(&src->read_before[i]);
    dst->write_before[i] = atomic64_read(&src->write_before[i]);
  }
}

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

void init_nvme_tcp_io_instance(struct nvme_tcp_io_instance *inst,
                               bool _is_write, int _req_tag, int _waitlist,
                               int _cnt, bool _contains_c2h, bool _contains_r2t,
                               bool _is_spoiled, int _size) {
  int i;
  for (i = 0; i < EVENT_NUM; i++) {
    inst->ts[i] = 0;
    inst->ts2[i] = 0;
    inst->trpt[i] = 0;
    inst->sizs[i] = 0;
  }
  inst->is_write = _is_write;
  inst->req_tag = _req_tag;
  inst->waitlist = _waitlist;
  inst->contains_c2h = _contains_c2h;
  inst->contains_r2t = _contains_r2t;
  inst->is_spoiled = _is_spoiled;
  inst->cnt = _cnt;
  inst->size = _size;
}

void print_io_instance(struct nvme_tcp_io_instance *inst) {
  int i;
  pr_info("req_tag: %d, cmdid: %d, waitlist: %d\n", inst->req_tag, inst->cmdid,
          inst->waitlist);
  for (i = 0; i < inst->cnt; i++) {
    char name[32];
    nvme_tcp_trpt_name(inst->trpt[i], name);
    pr_info("event%d, %llu, %s, size: %llu, ts2: %llu\n", i, inst->ts[i], name,
            inst->sizs[i], inst->ts2[i]);
  }
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
void on_nvme_tcp_queue_rq(void *ignore, struct request *req, bool *to_trace,
                          int len1, int len2, long long unsigned int time) {
  u32 qid;
  u16 cnt;
  struct bio *b;

  if (!ctrl || args->io_type + rq_data_dir(req) == 1) return;

  if (req->rq_disk && req->rq_disk->disk_name) {
    if (!is_same_dev_name(req->rq_disk->disk_name, args->dev)) return;
  } else {
    return;
  }

  /** get queue id */
  qid = (!req->q->queuedata) ? 0 : req->mq_hctx->queue_num + 1;

  /** ignore the request from the queue 0 (admin queue) */
  if (qid == 0) return;

  if (to_sample() && current_io == NULL) {
    b = req->bio;
    u64 lat = 0;
    u32 size = 0;
    while (b) {
      if (!lat) lat = __bio_issue_time(time) - bio_issue_time(&b->bi_issue);
      size += b->bi_iter.bi_size;
      b = b->bi_next;
    }
    current_io = kmalloc(sizeof(struct nvme_tcp_io_instance), GFP_KERNEL);
    init_nvme_tcp_io_instance(current_io, (rq_data_dir(req) == WRITE), req->tag,
                              len1 + len2, 0, false, false, false, size);
    current_io->before = lat;
    append_event(current_io, time, QUEUE_RQ, -1, 0);
    *to_trace = true;
  }
}

/*
 * 0: QUEUE_RQ
 * 1: QUEUE_REQUEST
 * 2: TRY_SEND
 * 3: TRY_SEND_CMD_PDU
 * 4: DONE_SEND_REQ
 * 5: HANDLE_C2H_DATA
 * */
bool is_standard_read(struct nvme_tcp_io_instance *io) {
  if (io->cnt < 6) {
    return false;
  }
  if (io->trpt[0] != QUEUE_RQ || io->trpt[1] != QUEUE_REQUEST ||
      io->trpt[2] != TRY_SEND || io->trpt[3] != TRY_SEND_CMD_PDU ||
      io->trpt[4] != DONE_SEND_REQ || io->trpt[5] != HANDLE_C2H_DATA ||
      io->trpt[io->cnt - 1] != PROCESS_NVME_CQE) {
    return false;
  }
  return true;
}

// void update_read_breakdown(struct nvme_tcp_io_instance *io,
//                            struct nvmetcp_read_breakdown *rb) {
//   pr_info("update_read_breakdown\n");
//   int i;
//   if (!is_standard_read(io)) {
//     pr_err("event sequence is not correct\n");
//     print_io_instance(io);
//     // BUG();
//     return;
//   }
//   rb->cnt++;
//   rb->t_inqueue += io->ts[2] - io->ts[1];
//   rb->t_reqcopy += io->ts[4] - io->ts[2];

//   /**  */
//   rb->t_waitproc += io->ts[5] - io->ts2[5];
//   rb->t_waiting += io->ts2[5] - io->ts[4];

//   for (i = 6; i < io->cnt; i++) {
//     if (io->trpt[i] == RECV_DATA) {
//       rb->t_datacopy += io->ts[i] - io->ts[i - 1];
//     } else if (io->trpt[i] == TRY_RECV) {
//       rb->t_waiting += io->ts[i] - io->ts[i - 1];
//     }
//   }
//   rb->t_endtoend += io->ts[io->cnt - 1] - io->ts[1];
//   pr_info("end to end %d\n", io->ts[io->cnt - 1] - io->ts[1]);
// }

/**
 *
 *
 * without r2t
  0	 QUEUE_RQ
  1	 QUEUE_REQUEST
  2	 TRY_SEND
  3	 TRY_SEND_CMD_PDU
  4	 TRY_SEND_DATA
    ... <TRY_SEND_DATA>
  5	 DONE_SEND_REQ
  6	 PROCESS_NVME_CQE

  with r2t
  0	 QUEUE_RQ
  1	 QUEUE_REQUEST
  2	 TRY_SEND
  3	 TRY_SEND_CMD_PDU
  4	 DONE_SEND_REQ
  5	 HANDLE_R2T
  6	 QUEUE_REQUEST
  7	 TRY_SEND
  8	 TRY_SEND_DATA_PDU
  9	 TRY_SEND_DATA
  10	 TRY_SEND_DATA
  11	 TRY_SEND_DATA
  12	 TRY_SEND_DATA
  13	 TRY_SEND_DATA
  14	 TRY_SEND_DATA
  15	 TRY_SEND_DATA
  16	 TRY_SEND_DATA
  17	 TRY_SEND_DATA
  18	 TRY_SEND_DATA
  19	 TRY_SEND_DATA
  20	 TRY_SEND_DATA
  21	 TRY_SEND_DATA
  22	 TRY_SEND_DATA
  23	 TRY_SEND_DATA
  24	 TRY_SEND_DATA
  25	 DONE_SEND_REQ
  26	 PROCESS_NVME_CQE

 *
*/
void update_write_breakdown(struct nvme_tcp_io_instance *io,
                            struct nvmetcp_write_breakdown *rb) {
  /** get the corresponding io instance based on request size */

  if (io->contains_r2t) {
    /** TODO: to remove the check when it is safe */
    if (io->cnt < 11) {
      pr_err("event number for write containing r2t is < 11 \n");
      return;
    }
    if (io->trpt[0] != QUEUE_RQ || io->trpt[1] != QUEUE_REQUEST ||
        io->trpt[2] != TRY_SEND || io->trpt[4] != DONE_SEND_REQ ||
        io->trpt[5] != HANDLE_R2T || io->trpt[6] != QUEUE_REQUEST ||
        io->trpt[7] != TRY_SEND || io->trpt[io->cnt - 2] != DONE_SEND_REQ ||
        io->trpt[io->cnt - 1] != PROCESS_NVME_CQE) {
      BUG();
      return;
    }

    rb->t_inqueue += io->ts[2] - io->ts[1];
    rb->t_inqueue += io->ts[7] - io->ts[6];
    rb->t_reqcopy += io->ts[4] - io->ts[2];
    rb->t_waiting += io->ts[5] - io->ts[4];
    rb->t_waiting += io->ts[io->cnt - 1] - io->ts[io->cnt - 2];
    rb->t_datacopy += io->ts[io->cnt - 2] - io->ts[7];

  } else {
    rb->t_inqueue += io->ts[2] - io->ts[1];
    rb->t_reqcopy += io->ts[3] - io->ts[2];
    rb->t_datacopy += io->ts[io->cnt - 2] - io->ts[3];
    rb->t_waiting += io->ts[io->cnt - 1] - io->ts[io->cnt - 2];
  }
  rb->t_endtoend += io->ts[io->cnt - 1] - io->ts[0];
  rb->cnt++;
}

void nvme_tcp_stat_update(u64 now) {
  copy_nvme_tcp_stat(a_nvme_tcp_stat, &shared_nvme_tcp_stat->all_time_stat);
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int cmdid,
                               bool is_initial, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, QUEUE_REQUEST, -1, 0);
    current_io->cmdid = cmdid;
  }
}

void on_nvme_tcp_try_send(void *ignore, struct request *req,
                          long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND, -1, 0);
  }
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int len,
                                  int qid, int local_port,
                                  long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (args->qid[qid] && qid2port[qid] == -1) {
    qid2port[qid] = local_port;
    pr_info("set qid %d to port %d\n", qid, local_port);
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_CMD_PDU, -1, 0);
  }
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int len,
                                   long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_DATA_PDU, -1, 0);
  }
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int len,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_DATA, -1, 0);
  }
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, DONE_SEND_REQ, -1, 0);
  }
}

void on_nvme_tcp_try_recv(void *ignore, int offset, size_t len, int recv_stat,
                          int qid, unsigned long long time,
                          long long recv_time) {
  // if (req->tag == current_io->req_tag) {
  // append_event(current_io, time, TRY_RECV);
  // }
  // pr_info("on_nvme_tcp_try_recv\n");
  if (!ctrl) {
    return;
  }
  if (current_io && current_io->contains_c2h) {
    /* TODO: to remove this test */
    if (current_io->is_write) {
      pr_err("write io contains c2h data\n");
    }
    // current_io->sizs[current_io->cnt] = len;
    append_event(current_io, time, TRY_RECV, len, recv_time);
  }
}

// void on_nvme_tcp_recv_pdu(void *ignore, int consumed, unsigned char pdu_type,
//                           int qid, unsigned long long time) {
//   return;
// }

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq,
                                 int data_remain, int qid,
                                 unsigned long long time, long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }
  if (current_io && rq->tag == current_io->req_tag) {
    append_event(current_io, time, HANDLE_C2H_DATA, data_remain, recv_time);
    current_io->contains_c2h = true;
  }
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int cp_len,
                           int qid, unsigned long long time,
                           long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }
  if (current_io && rq->tag == current_io->req_tag) {
    append_event(current_io, time, RECV_DATA, cp_len, recv_time);
  }
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req,
                            unsigned long long time, long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    current_io->contains_r2t = true;
    append_event(current_io, time, HANDLE_R2T, -1, recv_time);
  }
}

void update_atomic_read_breakdown(struct nvme_tcp_io_instance *io,
                                  struct atomic_nvme_tcp_read_breakdown *rb) {
  int i;
  if (!is_standard_read(io)) {
    pr_err("event sequence is not correct\n");
    print_io_instance(io);
    // BUG();
    return;
  }
  atomic64_inc(&rb->cnt);
  atomic64_add(io->ts[2] - io->ts[1], &rb->t_inqueue);
  atomic64_add(io->ts[4] - io->ts[2], &rb->t_reqcopy);

  /**  */
  atomic64_add(io->ts[5] - io->ts2[5], &rb->t_waitproc);
  atomic64_add(io->ts2[5] - io->ts[4], &rb->t_waiting);

  for (i = 6; i < io->cnt; i++) {
    if (io->trpt[i] == RECV_DATA) {
      atomic64_add(io->ts[i] - io->ts[i - 1], &rb->t_datacopy);
    } else if (io->trpt[i] == TRY_RECV) {
      atomic64_add(io->ts[i] - io->ts[i - 1], &rb->t_waiting);
    }
  }
  atomic64_add(io->ts[io->cnt - 1] - io->ts[1], &rb->t_endtoend);
}

void update_atomic_write_breakdown(struct nvme_tcp_io_instance *io,
                                   struct atomic_nvme_tcp_write_breakdown *wb) {
  if (io->contains_r2t) {
    if (io->cnt < 11) {
      pr_err("event number for write containing r2t is < 11 \n");
      return;
    }
    if (io->trpt[0] != QUEUE_RQ || io->trpt[1] != QUEUE_REQUEST ||
        io->trpt[2] != TRY_SEND || io->trpt[4] != DONE_SEND_REQ ||
        io->trpt[5] != HANDLE_R2T || io->trpt[6] != QUEUE_REQUEST ||
        io->trpt[7] != TRY_SEND || io->trpt[io->cnt - 2] != DONE_SEND_REQ ||
        io->trpt[io->cnt - 1] != PROCESS_NVME_CQE) {
      BUG();
      return;
    }

    atomic64_add(io->ts[2] - io->ts[1], &wb->t_inqueue);
    atomic64_add(io->ts[7] - io->ts[6], &wb->t_inqueue);
    atomic64_add(io->ts[4] - io->ts[2], &wb->t_reqcopy);
    atomic64_add(io->ts[5] - io->ts[4], &wb->t_waiting);
    atomic64_add(io->ts[io->cnt - 1] - io->ts[io->cnt - 2], &wb->t_waiting);
    atomic64_add(io->ts[io->cnt - 2] - io->ts[7], &wb->t_datacopy);

  } else {
    atomic64_add(io->ts[2] - io->ts[1], &wb->t_inqueue);
    atomic64_add(io->ts[3] - io->ts[2], &wb->t_reqcopy);
    atomic64_add(io->ts[io->cnt - 2] - io->ts[3], &wb->t_datacopy);
    atomic64_add(io->ts[io->cnt - 1] - io->ts[io->cnt - 2], &wb->t_waiting);
  }
  atomic64_add(io->ts[io->cnt - 1] - io->ts[0], &wb->t_endtoend);
  atomic64_inc(&wb->cnt);
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req,
                                  unsigned long long time,
                                  long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, PROCESS_NVME_CQE, -1, recv_time);

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
  // pr_info("register nvme_tcp_recv_pdu");
  // if ((ret = register_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL))) {
  //   pr_err("Failed to register nvme_tcp_recv_pdu\n");
  //   goto unregister_try_recv;
  // }
  pr_info("register nvme_tcp_handle_c2h_data");
  if ((ret = register_trace_nvme_tcp_handle_c2h_data(
           on_nvme_tcp_handle_c2h_data, NULL))) {
    pr_err("Failed to register nvme_tcp_handle_c2h_data\n");
    goto unregister_try_recv;
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
// unregister_recv_pdu:
//   unregister_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL);
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
  // unregister_trace_nvme_tcp_recv_pdu(on_nvme_tcp_recv_pdu, NULL);
  unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
  unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
  unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
  unregister_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe,
                                             NULL);
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
  int i;
  current_io = NULL;

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