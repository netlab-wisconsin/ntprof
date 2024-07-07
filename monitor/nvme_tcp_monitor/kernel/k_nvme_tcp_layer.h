#ifndef K_NVMETCP_LAYER_H
#define K_NVMETCP_LAYER_H

// #include <linux/blkdev.h>
#include <linux/types.h>

#include "ntm_com.h"

struct atomic_nvme_tcp_read_breakdown {
  atomic64_t cnt;
  atomic64_t t_inqueue;
  atomic64_t t_reqcopy;
  atomic64_t t_datacopy;
  atomic64_t t_waiting;
  atomic64_t t_waitproc;
  atomic64_t t_endtoend;
};

struct atomic_nvme_tcp_write_breakdown {
  atomic64_t cnt;
  atomic64_t t_inqueue;
  atomic64_t t_reqcopy;
  atomic64_t t_datacopy;
  atomic64_t t_waiting;
  atomic64_t t_endtoend;
};

struct atomic_nvme_tcp_stat {
  struct atomic_nvme_tcp_read_breakdown read[SIZE_NUM];
  struct atomic_nvme_tcp_write_breakdown write[SIZE_NUM];
  atomic64_t read_before[SIZE_NUM];
  atomic64_t write_before[SIZE_NUM];
};

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