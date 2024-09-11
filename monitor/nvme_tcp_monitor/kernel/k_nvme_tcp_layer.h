#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

// #include <linux/blkdev.h>
#include <linux/atomic.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "ntm_com.h"

struct atomic_nvme_tcp_read_breakdown {
  atomic64_t cnt;
  atomic64_t sub_q;
  atomic64_t req_proc;
  atomic64_t waiting;
  atomic64_t comp_q;
  atomic64_t resp_proc;
  atomic64_t e2e;
  atomic64_t trans;
};

static inline void init_atomic_nvme_tcp_read_breakdown(
    struct atomic_nvme_tcp_read_breakdown *rb) {
  atomic64_set(&rb->cnt, 0);
  atomic64_set(&rb->sub_q, 0);
  atomic64_set(&rb->req_proc, 0);
  atomic64_set(&rb->waiting, 0);
  atomic64_set(&rb->comp_q, 0);
  atomic64_set(&rb->resp_proc, 0);
  atomic64_set(&rb->e2e, 0);
  atomic64_set(&rb->trans, 0);
}

static inline void copy_nvme_tcp_read_breakdown(
    struct atomic_nvme_tcp_read_breakdown *src,
    struct nvmetcp_read_breakdown *dst) {
  dst->cnt = atomic64_read(&src->cnt);
  dst->sub_q = atomic64_read(&src->sub_q);
  dst->req_proc = atomic64_read(&src->req_proc);
  dst->waiting = atomic64_read(&src->waiting);
  dst->comp_q = atomic64_read(&src->comp_q);
  dst->resp_proc = atomic64_read(&src->resp_proc);
  dst->e2e = atomic64_read(&src->e2e);
  dst->trans = atomic64_read(&src->trans);
}

struct atomic_nvme_tcp_write_breakdown {
  atomic64_t cnt;
  atomic64_t sub_q1;
  atomic64_t req_proc1;
  atomic64_t waiting1;
  atomic64_t ready_q;
  atomic64_t sub_q2;
  atomic64_t req_proc2;
  atomic64_t waiting2;
  atomic64_t comp_q;
  atomic64_t e2e;

  atomic64_t trans1;
  atomic64_t trans2;
  atomic64_t cnt2;
};

static inline void init_atomic_nvme_tcp_write_breakdown(
    struct atomic_nvme_tcp_write_breakdown *wb) {
  atomic64_set(&wb->cnt, 0);
  atomic64_set(&wb->sub_q1, 0);
  atomic64_set(&wb->req_proc1, 0);
  atomic64_set(&wb->waiting1, 0);
  atomic64_set(&wb->ready_q, 0);
  atomic64_set(&wb->sub_q2, 0);
  atomic64_set(&wb->req_proc2, 0);
  atomic64_set(&wb->waiting2, 0);
  atomic64_set(&wb->comp_q, 0);
  atomic64_set(&wb->e2e, 0);
  atomic64_set(&wb->trans1, 0);
  atomic64_set(&wb->trans2, 0);
  atomic64_set(&wb->cnt2, 0);
}

static inline void copy_nvme_tcp_write_breakdown(
    struct atomic_nvme_tcp_write_breakdown *src,
    struct nvmetcp_write_breakdown *dst) {
  dst->cnt = atomic64_read(&src->cnt);
  dst->sub_q1 = atomic64_read(&src->sub_q1);
  dst->req_proc1 = atomic64_read(&src->req_proc1);
  dst->waiting1 = atomic64_read(&src->waiting1);
  dst->ready_q = atomic64_read(&src->ready_q);
  dst->sub_q2 = atomic64_read(&src->sub_q2);
  dst->req_proc2 = atomic64_read(&src->req_proc2);
  dst->waiting2 = atomic64_read(&src->waiting2);
  dst->comp_q = atomic64_read(&src->comp_q);
  dst->e2e = atomic64_read(&src->e2e);
  dst->trans1 = atomic64_read(&src->trans1);
  dst->trans2 = atomic64_read(&src->trans2);
  dst->cnt2 = atomic64_read(&src->cnt2);
}

struct atomic_nvme_tcp_stat {
  struct atomic_nvme_tcp_read_breakdown read[SIZE_NUM];
  struct atomic_nvme_tcp_write_breakdown write[SIZE_NUM];
  atomic64_t read_before[SIZE_NUM];
  atomic64_t write_before[SIZE_NUM];

  /* 0. flush
     1. read, 0K < s <= 4K
     2. read, 4K < s <= 8K
     3. read, 8K < s <= 16K
     4. read, 16K < s <= 32K
     5. read, 32K < s <= 64K
     6. read, 64K < s <= 128K
     7. read, s > 128K
     8. write, 0K < s <= 4K
     9. write, 4K < s <= 8K
     10. write, 8K < s <= 16K
     11. write, 16K < s <= 32K
     12. write, 32K < s <= 64K
     13. write, 64K < s <= 128K
     14. write, s > 128K
     15. others
  */
  atomic64_t req_type_hist[16];
};

static inline void inc_req_type_hist(struct atomic_nvme_tcp_stat *stat, int idx) {
  atomic64_inc(&stat->req_type_hist[idx]);
}

static inline void copy_nvme_tcp_stat(struct atomic_nvme_tcp_stat *src,
                                      struct nvme_tcp_stat *dst) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    copy_nvme_tcp_read_breakdown(&src->read[i], &dst->read[i]);
    copy_nvme_tcp_write_breakdown(&src->write[i], &dst->write[i]);
    dst->read_before[i] = atomic64_read(&src->read_before[i]);
    dst->write_before[i] = atomic64_read(&src->write_before[i]);
  }
  for (i = 0; i < 16; i++) {
    dst->req_type_hist[i] = atomic64_read(&src->req_type_hist[i]);
  }
}

static inline void inc_req_hist(struct atomic_nvme_tcp_stat *stat, int size, int op) {
  if(op == 0){
    // read
    if(size <= 4096) {
      inc_req_type_hist(stat, 1);
    } else if(size <= 8192) {
      inc_req_type_hist(stat, 2);
    } else if(size <= 16384) {
      inc_req_type_hist(stat, 3);
    } else if(size <= 32768) {
      inc_req_type_hist(stat, 4);
    } else if(size <= 65536) {
      inc_req_type_hist(stat, 5);
    } else if(size <= 131072) {
      inc_req_type_hist(stat, 6);
    } else {
      inc_req_type_hist(stat, 7);
    }
  } else if (op == 1) {
    // write
    if(size <= 4096) {
      inc_req_type_hist(stat, 8);
    } else if(size <= 8192) {
      inc_req_type_hist(stat, 9);
    } else if(size <= 16384) {
      inc_req_type_hist(stat, 10);
    } else if(size <= 32768) {
      inc_req_type_hist(stat, 11);
    } else if(size <= 65536) {
      inc_req_type_hist(stat, 12);
    } else if(size <= 131072) {
      inc_req_type_hist(stat, 13);
    } else {
      inc_req_type_hist(stat, 14);
    }
  } else if (op == 2) {
    // flush
    inc_req_type_hist(stat, 0);
  } else {
    pr_info("unknown op: %d\n", op);
  }
}

static inline void init_atomic_nvme_tcp_stat(
    struct atomic_nvme_tcp_stat *stat) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    init_atomic_nvme_tcp_read_breakdown(&stat->read[i]);
    init_atomic_nvme_tcp_write_breakdown(&stat->write[i]);
    atomic64_set(&stat->read_before[i], 0);
    atomic64_set(&stat->write_before[i], 0);
  }
  for(i = 0; i < 16; i++) { atomic64_set(&stat->req_type_hist[i], 0); }
}

struct atomic_nvmetcp_throughput {
  atomic64_t first_ts;
  atomic64_t last_ts;
  atomic64_t read_cnt[SIZE_NUM];
  atomic64_t write_cnt[SIZE_NUM];
};

static void inline init_atomic_nvmetcp_throughput(
    struct atomic_nvmetcp_throughput *tp) {
  atomic64_set(&tp->first_ts, 0);
  atomic64_set(&tp->last_ts, 0);
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    atomic64_set(&tp->read_cnt[i], 0);
    atomic64_set(&tp->write_cnt[i], 0);
  }
}

static void inline copy_nvmetcp_throughput(
    struct atomic_nvmetcp_throughput *src, struct nvme_tcp_throughput *dst) {
  dst->first_ts = atomic64_read(&src->first_ts);
  dst->last_ts = atomic64_read(&src->last_ts);
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    dst->read_cnt[i] = atomic64_read(&src->read_cnt[i]);
    dst->write_cnt[i] = atomic64_read(&src->write_cnt[i]);
  }
}

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

#define EVENT_NUM 64

static inline void nvme_tcp_trpt_name(enum nvme_tcp_trpt trpt, char *name) {
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
  int cmdid;
  u64 ts[EVENT_NUM];
  u64 ts2[EVENT_NUM];
  enum nvme_tcp_trpt trpt[EVENT_NUM];
  // size of data for an event, such as a receiving event
  u64 sizs[EVENT_NUM];
  int cnt;
  // this is only useful for read io, maybe separate read and write io
  bool contains_c2h;
  // this is only for write io
  bool contains_r2t;

  /** indicate the event number exceeds the capacity, this sample is useless */
  bool is_spoiled;
  int size;    // size of the whole request
  u64 before;  // time between bio issue and entering the nvme_tcp_layer
  int qid;

  // the size of this array is 2, because for write request with r2t, it will
  // send 2 mesages the second element only makes sense for write request with
  // r2t
  int send_size[2];
  int estimated_transmission_time[2];  // the estimated transmission time for
                                       // each message
};

static inline void init_nvme_tcp_io_instance(struct nvme_tcp_io_instance *inst,
                                             bool _is_write, int _req_tag,
                                             int _waitlist, int _cnt,
                                             bool _contains_c2h,
                                             bool _contains_r2t,
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
  inst->send_size[0] = 0;
  inst->send_size[1] = 0;
  inst->estimated_transmission_time[0] = 0;
  inst->estimated_transmission_time[1] = 0;
}

#define BIG_NUM 1720748973000000000

static inline void print_io_instance(struct nvme_tcp_io_instance *inst) {
  int i;
  pr_info("req_tag: %d, cmdid: %d, waitlist: %d, e2e: %llu\n", inst->req_tag,
          inst->cmdid, inst->waitlist, inst->ts[inst->cnt - 1] - inst->ts[1]);
  for (i = 0; i < inst->cnt; i++) {
    char name[32];
    nvme_tcp_trpt_name(inst->trpt[i], name);
    pr_info("event%d, %llu, %s, size: %llu, ts2: %llu\n", i,
            inst->ts[i] - BIG_NUM, name, inst->sizs[i], inst->ts2[i]);
  }
  pr_info("size0: %d, size1: %d", inst->send_size[0], inst->send_size[1]);
  pr_info("estimated_transmission_time0: %d, estimated_transmission_time1: %d",
          inst->estimated_transmission_time[0],
          inst->estimated_transmission_time[1]);
}

void nvme_tcp_stat_update(u64 now);

int init_nvme_tcp_layer_monitor(void);

void exit_nvme_tcp_layer_monitor(void);

#endif  // K_NVMETCP_LAYER_H