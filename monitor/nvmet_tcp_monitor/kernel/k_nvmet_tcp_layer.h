#ifndef _K_NVME_TCP_LAYER_H_
#define _K_NVME_TCP_LAYER_H_

#include <linux/string.h>
#include <linux/types.h>
#include <linux/atomic.h>

#include "nttm_com.h"

#define EVENT_NUM 64

/**
 * tracepoints:
 * nvmet_tcp:nvmet_tcp_try_recv_pdu
 * nvmet_tcp:nvmet_tcp_done_recv_pdu
 * nvmet_tcp:nvmet_tcp_exec_read_req
 * nvmet_tcp:nvmet_tcp_exec_write_req
 * nvmet_tcp:nvmet_tcp_queue_response
 * nvmet_tcp:nvmet_setup_c2h_data_pdu
 * nvmet_tcp:nvmet_setup_r2t_pdu
 * nvmet_tcp:nvmet_setup_response_pdu
 * nvmet_tcp:nvmet_try_send_data_pdu
 * nvmet_tcp:nvmet_try_send_r2t
 * nvmet_tcp:nvmet_try_send_response
 * nvmet_tcp:nvmet_try_send_data
 * nvmet_tcp:nvmet_tcp_handle_h2c_data_pdu
 * nvmet_tcp:nvmet_tcp_try_recv_data
 *
 */

enum nvmet_tcp_trpt {
  TRY_RECV_PDU,
  DONE_RECV_PDU,
  EXEC_READ_REQ,
  EXEC_WRITE_REQ,
  QUEUE_RESPONSE,
  SETUP_C2H_DATA_PDU,
  SETUP_R2T_PDU,
  SETUP_RESPONSE_PDU,
  TRY_SEND_DATA_PDU,
  TRY_SEND_R2T,
  TRY_SEND_RESPONSE,
  TRY_SEND_DATA,
  HANDLE_H2C_DATA_PDU,
  TRY_RECV_DATA
};

static inline void nvmet_tcp_trpt_name(enum nvmet_tcp_trpt p, char* name) {
  switch (p) {
    case TRY_RECV_PDU:
      strcpy(name, "TRY_RECV_PDU");
      break;
    case DONE_RECV_PDU:
      strcpy(name, "DONE_RECV_PDU");
      break;
    case EXEC_READ_REQ:
      strcpy(name, "EXEC_READ_REQ");
      break;
    case EXEC_WRITE_REQ:
      strcpy(name, "EXEC_WRITE_REQ");
      break;
    case QUEUE_RESPONSE:
      strcpy(name, "QUEUE_RESPONSE");
      break;
    case SETUP_C2H_DATA_PDU:
      strcpy(name, "SETUP_C2H_DATA_PDU");
      break;
    case SETUP_R2T_PDU:
      strcpy(name, "SETUP_R2T_PDU");
      break;
    case SETUP_RESPONSE_PDU:
      strcpy(name, "SETUP_RESPONSE_PDU");
      break;
    case TRY_SEND_DATA_PDU:
      strcpy(name, "TRY_SEND_DATA_PDU");
      break;
    case TRY_SEND_R2T:
      strcpy(name, "TRY_SEND_R2T");
      break;
    case TRY_SEND_RESPONSE:
      strcpy(name, "TRY_SEND_RESPONSE");
      break;
    case TRY_SEND_DATA:
      strcpy(name, "TRY_SEND_DATA");
      break;
    case HANDLE_H2C_DATA_PDU:
      strcpy(name, "HANDLE_H2C_DATA_PDU");
      break;
    case TRY_RECV_DATA:
      strcpy(name, "TRY_RECV_DATA");
      break;
    default:
      strcpy(name, "UNKNOWN");
      break;
  }
}

struct nvmet_io_instance {
  bool is_write;
  u16 command_id;
  u64 ts[EVENT_NUM];
  enum nvmet_tcp_trpt trpt[EVENT_NUM];
  u8 cnt;
  u32 size;
  bool is_spoiled;
  bool contain_r2t;
};

static inline void init_nvmet_tcp_io_instance(
    struct nvmet_io_instance* io_instance, u16 command_id, bool is_write,
    u32 size) {
  int i;
  io_instance->is_write = is_write;
  io_instance->command_id = command_id;
  io_instance->size = size;
  io_instance->cnt = 0;
  io_instance->is_spoiled = false;
  io_instance->contain_r2t = false;
  for (i = 0; i < EVENT_NUM; i++) {
    io_instance->ts[i] = 0;
  }
}

struct atomic_nvmet_tcp_read_breakdown {
  atomic64_t in_nvmet_tcp_time;
  atomic64_t in_blk_time;
  atomic64_t end2end_time;
  atomic_t cnt;
};

static inline void init_atomic_nvmet_tcp_read_breakdown(
    struct atomic_nvmet_tcp_read_breakdown* breakdown) {
  atomic64_set(&breakdown->in_nvmet_tcp_time, 0);
  atomic64_set(&breakdown->in_blk_time, 0);
  atomic64_set(&breakdown->end2end_time, 0);
  atomic_set(&breakdown->cnt, 0);
}

static inline void copy_nvmet_tcp_read_breakdown(
    struct nvmet_tcp_read_breakdown* dst,
    struct atomic_nvmet_tcp_read_breakdown* src) {
  dst->in_nvmet_tcp_time = atomic64_read(&src->in_nvmet_tcp_time);
  dst->in_blk_time = atomic64_read(&src->in_blk_time);
  dst->end2end_time = atomic64_read(&src->end2end_time);
  dst->cnt = atomic_read(&src->cnt);
}

struct atomic_nvmet_tcp_write_breakdown {
  atomic64_t make_r2t_time;
  atomic64_t in_nvmet_tcp_time;
  atomic64_t in_blk_time;
  atomic64_t end2end_time;
  atomic_t cnt;
};

static inline void init_atomic_nvmet_tcp_write_breakdown(
    struct atomic_nvmet_tcp_write_breakdown* breakdown) {
  atomic64_set(&breakdown->make_r2t_time, 0);
  atomic64_set(&breakdown->in_nvmet_tcp_time, 0);
  atomic64_set(&breakdown->in_blk_time, 0);
  atomic64_set(&breakdown->end2end_time, 0);
  atomic_set(&breakdown->cnt, 0);
}

static inline void copy_nvmet_tcp_write_breakdown(
    struct nvmet_tcp_write_breakdown* dst,
    struct atomic_nvmet_tcp_write_breakdown* src) {
  dst->make_r2t_time = atomic64_read(&src->make_r2t_time);
  dst->in_nvmet_tcp_time = atomic64_read(&src->in_nvmet_tcp_time);
  dst->in_blk_time = atomic64_read(&src->in_blk_time);
  dst->end2end_time = atomic64_read(&src->end2end_time);
  dst->cnt = atomic_read(&src->cnt);
}

struct atomic_nvmet_tcp_stat {
  struct atomic_nvmet_tcp_read_breakdown read_breakdown[SIZE_NUM];
  struct atomic_nvmet_tcp_write_breakdown write_breakdown[SIZE_NUM];
};

static inline void init_atomic_nvmet_tcp_stat(
    struct atomic_nvmet_tcp_stat* stat) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    init_atomic_nvmet_tcp_read_breakdown(&stat->read_breakdown[i]);
    init_atomic_nvmet_tcp_write_breakdown(&stat->write_breakdown[i]);
  }
}

static inline void copy_nvmet_tcp_stat(
    struct nvmet_tcp_stat* dst, struct atomic_nvmet_tcp_stat* src) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    copy_nvmet_tcp_read_breakdown(&dst->all_read[i], &src->read_breakdown[i]);
    copy_nvmet_tcp_write_breakdown(&dst->all_write[i], &src->write_breakdown[i]);
  }
}


void nvmet_tcp_stat_update(u64 now);
int init_nvmet_tcp_layer(void);
void exit_nvmet_tcp_layer(void);

#endif  // _K_NVME_TCP_LAYER_H_