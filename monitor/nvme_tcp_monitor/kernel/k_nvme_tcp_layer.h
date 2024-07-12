#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

// #include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/atomic.h>

#include "ntm_com.h"

struct atomic_nvme_tcp_read_breakdown {
  atomic64_t cnt;
  atomic64_t sub_q;
  atomic64_t req_proc;
  atomic64_t waiting;
  atomic64_t comp_q;
  atomic64_t resp_proc;
  atomic64_t e2e;
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
}

static inline void copy_nvme_tcp_read_breakdown(struct atomic_nvme_tcp_read_breakdown *src,
                                  struct nvmetcp_read_breakdown *dst) {
  dst->cnt = atomic64_read(&src->cnt);
  dst->sub_q = atomic64_read(&src->sub_q);
  dst->req_proc = atomic64_read(&src->req_proc);
  dst->waiting = atomic64_read(&src->waiting);
  dst->comp_q = atomic64_read(&src->comp_q);
  dst->resp_proc = atomic64_read(&src->resp_proc);
  dst->e2e = atomic64_read(&src->e2e);
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
}

static inline void copy_nvme_tcp_write_breakdown(struct atomic_nvme_tcp_write_breakdown *src,
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
}

struct atomic_nvme_tcp_stat {
  struct atomic_nvme_tcp_read_breakdown read[SIZE_NUM];
  struct atomic_nvme_tcp_write_breakdown write[SIZE_NUM];
  atomic64_t read_before[SIZE_NUM];
  atomic64_t write_before[SIZE_NUM];
};

static inline void copy_nvme_tcp_stat(struct atomic_nvme_tcp_stat *src,
                        struct nvme_tcp_stat *dst) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    copy_nvme_tcp_read_breakdown(&src->read[i], &dst->read[i]);
    copy_nvme_tcp_write_breakdown(&src->write[i], &dst->write[i]);
    dst->read_before[i] = atomic64_read(&src->read_before[i]);
    dst->write_before[i] = atomic64_read(&src->write_before[i]);
  }
}

static inline void init_atomic_nvme_tcp_stat(struct atomic_nvme_tcp_stat *stat) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    init_atomic_nvme_tcp_read_breakdown(&stat->read[i]);
    init_atomic_nvme_tcp_write_breakdown(&stat->write[i]);
    atomic64_set(&stat->read_before[i], 0);
    atomic64_set(&stat->write_before[i], 0);
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
};

void nvme_tcp_stat_update(u64 now);

int init_nvme_tcp_layer_monitor(void);

void exit_nvme_tcp_layer_monitor(void);

#endif  // K_NVMETCP_LAYER_H