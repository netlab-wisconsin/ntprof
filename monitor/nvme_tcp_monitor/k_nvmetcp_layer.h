#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/nvme.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>

#include "ntm_com.h"
#include "util.h"

#define EVENT_NUM 64

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
  bool is_write;
  int req_tag;
  int waitlist;
  u64 ts[EVENT_NUM];
  u64 ts2[EVENT_NUM];
  enum nvme_tcp_trpt trpt[EVENT_NUM];
  u64 sizs[EVENT_NUM];
  int cnt;

  // this is only useful for read io, maybe separate read and write io
  bool contains_c2h;
  // this is only for write io
  bool contains_r2t;

  /** indicate the event number exceeds the capacity, this sample is useless*/
  bool is_spoiled;
  int size;  // request size
};

void init_nvme_tcp_io_instance(struct nvme_tcp_io_instance *inst,
                               bool _is_write, int _req_tag, int _waitlist,
                               int _cnt, bool _contains_c2h, bool _contains_r2t,
                               bool _is_spoiled, int _size) {
  inst->is_write = _is_write;
  inst->req_tag = _req_tag;
  inst->waitlist = _waitlist;
  inst->contains_c2h = _contains_c2h;
  inst->contains_r2t = _contains_r2t;
  inst->is_spoiled = _is_spoiled;
  inst->cnt = _cnt;
  inst->size = _size;
  int i;
  for (i = 0; i < EVENT_NUM; i++) {
    inst->ts[i] = 0;
    inst->ts2[i] = 0;
    inst->trpt[i] = 0;
    inst->sizs[i] = 0;
  }
}

void print_io_instance(struct nvme_tcp_io_instance *inst) {
  pr_info("req_tag: %d, waitlist: %d\n", inst->req_tag, inst->waitlist);
  int i;
  for (i = 0; i < inst->cnt; i++) {
    char name[32];
    nvme_tcp_trpt_name(inst->trpt[i], name);
    pr_info("ts: %llu, trpt: %s, size: %llu, ts2: %llu\n", inst->ts[i], name,
            inst->sizs[i], inst->ts2[i]);
  }
}

static struct nvme_tcp_io_instance *current_io = NULL;

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

struct _blk_lat_stat {
  atomic64_t sum_blk_layer_lat;
  atomic64_t cnt;
};

void _init_nvmetcp_stat(struct _blk_lat_stat *stat) {
  atomic64_set(&stat->sum_blk_layer_lat, 0);
  atomic64_set(&stat->cnt, 0);
}

void copy_nvmetcp_stat(struct _blk_lat_stat *src, struct blk_lat_stat *dst) {
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

static struct sliding_window *sw_nvmetcp_io_samples;

static struct _blk_lat_stat *_raw_blk_lat_stat;

// static struct nvmetcp_read_breakdown *read_breakdown;

// static struct nvmetcp_write_breakdown *write_breakdown;

struct proc_dir_entry *entry_nvmetcp_dir;
struct proc_dir_entry *entry_nvme_tcp_stat;

struct nvme_tcp_stat *nvme_tcp_stat;

/**
 * This function is called when the nvme_tcp_queue_rq tracepoint is triggered
 * @param ignore: the first parameter of the tracepoint
 * @param req: the request
 */
void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int len1, int len2,
                          long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }

  /** get queue id */
  u32 qid = (!req->q->queuedata) ? 0 : req->mq_hctx->queue_num + 1;

  /** ignore the request from the queue 0 (admin queue) */
  if (qid == 0) {
    return;
  }

  u16 cnt = 0;

  struct bio *b;
  b = req->bio;
  while (b) {
    u64 _start = bio_issue_time(&b->bi_issue);
    u64 _now = __bio_issue_time(time);
    /** the unit of lat is ns */
    u64 lat = _now - _start;
    cnt++;
    b = b->bi_next;

    atomic64_add(lat, &_raw_blk_lat_stat->sum_blk_layer_lat);
    atomic64_inc(&_raw_blk_lat_stat->cnt);

    if (to_sample()) {
      u64 *p_lat = kmalloc(sizeof(u64), GFP_KERNEL);
      (*p_lat) = lat;
      struct sw_node *node = kmalloc(sizeof(struct sw_node), GFP_KERNEL);
      node->data = p_lat;
      node->timestamp = time;
      add_to_sliding_window(sw_blk_layer_time, node);

      /** initialize current io */
      if (current_io == NULL) {
        // pr_info("initialize current_io\n");
        current_io = kmalloc(sizeof(struct nvme_tcp_io_instance), GFP_KERNEL);
        init_nvme_tcp_io_instance(current_io, (rq_data_dir(req) == WRITE),
                                  req->tag, len1 + len2, 0, false, false, false,
                                  0);
        append_event(current_io, time, QUEUE_RQ, -1, 0);
      } else {
        /**
         * according the sample rate, we hope to keep info about this io,
         * but we need to make sure the previous io is processed completely
         * so we ignore this io
         */
        pr_info("current_io is not NULL\n");
      }
    }
  }
}

void init_nvmetcp_write_breakdown(struct nvmetcp_write_breakdown *wb) {
  wb->cnt = 0;
  wb->t_inqueue = 0;
  wb->t_reqcopy = 0;
  wb->t_datacopy = 0;
  wb->t_waiting = 0;
  wb->t_endtoend = 0;
}

void init_nvmetcp_read_breakdown(struct nvmetcp_read_breakdown *rb) {
  rb->cnt = 0;
  rb->t_inqueue = 0;
  rb->t_reqcopy = 0;
  rb->t_datacopy = 0;
  rb->t_waiting = 0;
  rb->t_endtoend = 0;
  rb->t_waitproc = 0;
}

void init_nvme_tcp_stat(struct nvme_tcp_stat *stat) {
  init_blk_lat_stat(&stat->blk_lat);
  init_nvmetcp_read_breakdown(&stat->read);
  init_nvmetcp_write_breakdown(&stat->write);
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
      io->trpt[4] != DONE_SEND_REQ || io->trpt[5] != HANDLE_C2H_DATA
      || io->trpt[io->cnt-1] != PROCESS_NVME_CQE) {
    return false;
  }
  return true;
}

void update_read_breakdown(struct nvme_tcp_io_instance *io,
                           struct nvmetcp_read_breakdown *rb) {
  int i;
  if (!is_standard_read(io)) {
    pr_err("event sequence is not correct\n");
    print_io_instance(io);
    // BUG();
    return;
  }
  rb->cnt++;
  rb->t_inqueue += io->ts[2] - io->ts[1];
  rb->t_reqcopy += io->ts[4] - io->ts[2];

  /**  */
  rb->t_waitproc += io->ts[5] - io->ts2[5];
  rb->t_waiting += io->ts2[5] - io->ts[4];

  for (i = 6; i < io->cnt; i++) {
    if (io->trpt[i] == RECV_DATA) {
      rb->t_datacopy += io->ts[i] - io->ts[i - 1];
    } else if (io->trpt[i] == TRY_RECV) {
      rb->t_waiting += io->ts[i] - io->ts[i - 1];
    }
  }
  rb->t_endtoend += io->ts[io->cnt - 1] - io->ts[0];
}

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
  int i;
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

void analize_sw_latency_breakdown(struct sliding_window *sw) {
  init_nvmetcp_read_breakdown(&nvme_tcp_stat->read);
  init_nvmetcp_write_breakdown(&nvme_tcp_stat->write);
  struct list_head *pos, *q;
  /** traverse the linked list */
  spin_lock(&sw->lock);
  list_for_each_safe(pos, q, &sw->list) {
    struct sw_node *node = list_entry(pos, struct sw_node, list);
    struct nvme_tcp_io_instance *io = (struct nvme_tcp_io_instance *)node->data;
    if (io->is_spoiled) continue;
    if (io->is_write) {
      update_write_breakdown(io, &nvme_tcp_stat->write);
    } else {
      update_read_breakdown(io, &nvme_tcp_stat->read);
    }
  }
  spin_unlock(&sw->lock);
}

/**
 * a routine to update the sliding window
 */
void nvmetcp_stat_update(u64 now) {
  copy_nvmetcp_stat(_raw_blk_lat_stat, &nvme_tcp_stat->blk_lat);
  remove_from_sliding_window(sw_blk_layer_time, now - 10 * NSEC_PER_SEC);

  /** calculate the time of interest in the sliding window */
  analize_sw_latency_breakdown(sw_nvmetcp_io_samples);
  remove_from_sliding_window(sw_nvmetcp_io_samples, now - 10 * NSEC_PER_SEC);
}

/**
 * define call back functions for all tracepoints in nvme_tcp kayer
 */
void on_nvme_tcp_queue_request(void *ignore, struct request *req,
                               bool is_initial, long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, QUEUE_REQUEST, -1, 0);
  }
  // pr_info("on_nvme_tcp_queue_request\n");
}

void on_nvme_tcp_try_send(void *ignore, struct request *req,
                          long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND, -1, 0);
  }
  // pr_info("on_nvme_tcp_try_send\n");
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int len,
                                  long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_CMD_PDU, -1, 0);
  }
  // pr_info("on_nvme_tcp_try_send_cmd_pdu\n");
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int len,
                                   long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_DATA_PDU, -1, 0);
  }
  // pr_info("on_nvme_tcp_try_send_data_pdu\n");
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int len,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, TRY_SEND_DATA, -1, 0);
  }
  // pr_info("on_nvme_tcp_try_send_data\n");
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req,
                               long long unsigned int time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, DONE_SEND_REQ, -1, 0);
  }
  // pr_info("on_nvme_tcp_done_send_req\n");
}

void on_nvme_tcp_try_recv(void *ignore, int offset, size_t len, int recv_stat,
                          int qid, unsigned long long time) {
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
    append_event(current_io, time, TRY_RECV, len, 0);
  }
}

void on_nvme_tcp_recv_pdu(void *ignore, int consumed, unsigned char pdu_type,
                          int qid, unsigned long long time) {
  // if (req->tag == current_io->req_tag) {
  //   append_event(current_io, time, RECV_PDU);
  // }
  // pr_info("on_nvme_tcp_recv_pdu\n");
}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq,
                                 int data_remain, int qid,
                                 unsigned long long time,
                                 unsigned long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }
  if (current_io && rq->tag == current_io->req_tag) {
    append_event(current_io, time, HANDLE_C2H_DATA, data_remain, recv_time);
    current_io->contains_c2h = true;
  }
  // pr_info("on_nvme_tcp_handle_c2h_data\n");
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int cp_len,
                           int qid, unsigned long long time,
                           unsigned long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(rq) == 1) {
    return;
  }
  if (current_io && rq->tag == current_io->req_tag) {
    append_event(current_io, time, RECV_DATA, cp_len, recv_time);
  }
  // pr_info("on_nvme_tcp_recv_data\n");
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req,
                            unsigned long long time,
                            unsigned long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    current_io->contains_r2t = true;
    append_event(current_io, time, HANDLE_R2T, -1, recv_time);
  }
  // pr_info("on_nvme_tcp_handle_r2t\n");
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req,
                                  unsigned long long time,
                                  unsigned long long recv_time) {
  if (!ctrl || args->io_type + rq_data_dir(req) == 1) {
    return;
  }
  if (current_io && req->tag == current_io->req_tag) {
    append_event(current_io, time, PROCESS_NVME_CQE, -1, recv_time);

    // print_io_instance(current_io);

    /** append the io instance to the sliding window */
    struct sw_node *node = kmalloc(sizeof(struct sw_node), GFP_KERNEL);
    node->data = current_io;
    node->timestamp = current_io->ts[0];

    // if ((!current_io->is_write) && (!is_standard_read(current_io))) {
    //   pr_err("event sequence is not correct\n");
    //   print_io_instance(current_io);
    // }

    add_to_sliding_window(sw_nvmetcp_io_samples, node);

    current_io = NULL;
  }
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

static int mmap_nvme_tcp_stat(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(nvme_tcp_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_nvme_tcp_stat_fops = {
    .proc_mmap = mmap_nvme_tcp_stat,
};

int init_nvmetcp_proc_entries(void) {
  entry_nvmetcp_dir = proc_mkdir("nvmetcp", parent_dir);
  if (!entry_nvmetcp_dir) {
    pr_err("Failed to create proc entry nvmetcp\n");
    return -ENOMEM;
  }

  entry_nvme_tcp_stat =
      proc_create("stat", 0, entry_nvmetcp_dir, &ntm_nvme_tcp_stat_fops);
  if (!entry_nvme_tcp_stat) {
    pr_err("Failed to create proc entry stat\n");
    vfree(nvme_tcp_stat);
    return -ENOMEM;
  }
  return 0;
}

static void remove_nvmetcp_proc_entries(void) {
  remove_proc_entry("stat", entry_nvmetcp_dir);
  remove_proc_entry("nvmetcp", parent_dir);
}

static void unmmap_raw_nvmetcp_stat(struct file *file,
                                    struct vm_area_struct *vma) {
  pr_info("unmmap_raw_nvmetcp_stat\n");
}

int init_nvmetcp_variables(void) {
  _raw_blk_lat_stat = kmalloc(sizeof(*_raw_blk_lat_stat), GFP_KERNEL);
  if (!_raw_blk_lat_stat) -ENOMEM;
  _init_nvmetcp_stat(_raw_blk_lat_stat);

  nvme_tcp_stat = vmalloc(sizeof(*nvme_tcp_stat));
  if (!nvme_tcp_stat) {
    pr_err("Failed to allocate memory for nvme_tcp_stat\n");
    return -ENOMEM;
  }
  init_nvme_tcp_stat(nvme_tcp_stat);

  sw_blk_layer_time = kmalloc(sizeof(*sw_blk_layer_time), GFP_KERNEL);
  if (!sw_blk_layer_time) {
    pr_err("Failed to allocate memory for sw_blk_layer_time\n");
    return -ENOMEM;
  }
  init_sliding_window(sw_blk_layer_time);

  sw_nvmetcp_io_samples = kmalloc(sizeof(*sw_nvmetcp_io_samples), GFP_KERNEL);
  if (!sw_nvmetcp_io_samples) {
    pr_err("Failed to allocate memory for sw_nvmetcp_io_samples\n");
    return -ENOMEM;
  }
  init_sliding_window(sw_nvmetcp_io_samples);
  return 0;
}

int clear_nvmetcp_variables(void) {
  pr_info("clear_nvmetcp_variables\n");
  if (_raw_blk_lat_stat) {
    kfree(_raw_blk_lat_stat);
  }
  if (sw_blk_layer_time) {
    kfree(sw_blk_layer_time);
  }
  if (sw_nvmetcp_io_samples) {
    kfree(sw_nvmetcp_io_samples);
  }
  if (nvme_tcp_stat) {
    vfree(nvme_tcp_stat);
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