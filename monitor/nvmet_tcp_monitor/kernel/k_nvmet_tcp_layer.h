#ifndef _K_NVME_TCP_LAYER_H_
#define _K_NVME_TCP_LAYER_H_

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <trace/events/nvmet_tcp.h>

#include "k_nttm.h"
#include "nttm_com.h"
#include "util.h"

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

void nvmet_tcp_trpt_name(enum nvmet_tcp_trpt p, char* name) {
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

void init_nvmet_tcp_io_instance(struct nvmet_io_instance* io_instance,
                                u16 command_id, bool is_write, u32 size) {
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

void append_event(struct nvmet_io_instance* io_instance,
                  enum nvmet_tcp_trpt trpt, u64 ts) {
  if (io_instance->cnt < EVENT_NUM) {
    io_instance->trpt[io_instance->cnt] = trpt;
    io_instance->ts[io_instance->cnt] = ts;
    io_instance->cnt++;
  } else {
    io_instance->is_spoiled = true;
  }
}

void print_io_instance(struct nvmet_io_instance* io_instance) {
  char name[32];
  int i;
  pr_info("command_id: %d, is_write: %d, size: %d, cnt: %d, is_spoiled: %d\n",
          io_instance->command_id, io_instance->is_write, io_instance->size,
          io_instance->cnt, io_instance->is_spoiled);
  for (i = 0; i < io_instance->cnt; i++) {
    nvmet_tcp_trpt_name(io_instance->trpt[i], name);
    pr_info("event %d: %s, ts: %llu\n", i, name, io_instance->ts[i]);
  }
}

static struct nvmet_io_instance* current_io = NULL;

static struct sliding_window* sw_nvmet_tcp_io_samples;

struct proc_dir_entry* entry_nvmet_tcp_dir;
struct proc_dir_entry* entry_nvmet_tcp_stat;

static struct nvmet_tcp_stat* nvmettcp_stat;

// void to_track(u16 qid, bool is_write) {
//   if(!ctrl || !args->qid[qid] && args->io_type + is_write == 1) {
//     return false;
//   }
//   return true;
// }

void on_try_recv_pdu(void* ignore, u8 pdu_type, u8 hdr_len, int queue_left,
                     int qid, unsigned long long time) {
  // if (ctrl && args->qid[qid])
  // pr_info(
  //     "TRY_RECV_PDU: pdu_type: %d, hdr_len: %d, queue_left: %d, qid: %d, "
  //     "time: %llu\n",
  //     pdu_type, hdr_len, queue_left, qid, time);
}

void on_done_recv_pdu(void* ignore, u16 cmd_id, int qid, bool is_write,
                      unsigned long long time) {
  if (ctrl && args->qid[qid] && args->io_type + is_write != 1) {
    if (to_sample()) {
      // pr_info("DONE_RECV_PDU: cmd_id: %d, qid: %d, is_write: %d, time:
      // %llu\n",
      //         cmd_id, qid, is_write, time);
      if (!current_io) {
        current_io = kmalloc(sizeof(struct nvmet_io_instance), GFP_KERNEL);
        init_nvmet_tcp_io_instance(current_io, cmd_id, is_write, 0);
        append_event(current_io, DONE_RECV_PDU, time);
      } else {
        pr_info("current_io is not NULL\n");
      }
    }
  }
}

void on_exec_read_req(void* ignore, u16 cmd_id, int qid, bool is_write,
                      unsigned long long time) {
  if (is_write) {
    pr_err("exec_read_req: is_write is true\n");
  }
  if (ctrl && args->qid[qid]) {
    // pr_info("EXEC_READ_REQ: cmd_id: %d, qid: %d, is_write: %d, time: %llu\n",
    // cmd_id, qid, is_write, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, EXEC_READ_REQ, time);
    }
  }
}

void on_exec_write_req(void* ignore, u16 cmd_id, int qid, bool is_write,
                       unsigned long long time) {
  if (!is_write) {
    pr_err("exec_write_req: is_write is false\n");
  }
  if (ctrl && args->qid[qid]) {
    // pr_info("EXEC_WRITE_REQ: cmd_id: %d, qid: %d, is_write: %d, time:
    // %llu\n",
    //         cmd_id, qid, is_write, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, EXEC_WRITE_REQ, time);
    }
  }
}

void on_queue_response(void* ignore, u16 cmd_id, int qid, bool is_write,
                       unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("QUEUE_RESPONSE: cmd_id: %d, qid: %d, is_write: %d, time:
    // %llu\n",
    //         cmd_id, qid, is_write, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, QUEUE_RESPONSE, time);
    }
  }
}

void on_setup_c2h_data_pdu(void* ignore, u16 cmd_id, int qid,
                           unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("SETUP_C2H_DATA_PDU: cmd_id: %d, qid: %d, time: %llu\n", cmd_id,
    //         qid, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, SETUP_C2H_DATA_PDU, time);
    }
  }
}

void on_setup_r2t_pdu(void* ignore, u16 cmd_id, int qid,
                      unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("SETUP_R2T_PDU: cmd_id: %d, qid: %d, time: %llu\n", cmd_id, qid,
    //         time);
    if (current_io && current_io->command_id == cmd_id) {
      current_io->contain_r2t = true;
      append_event(current_io, SETUP_R2T_PDU, time);
    }
  }
}

void on_setup_response_pdu(void* ignore, u16 cmd_id, int qid,
                           unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("SETUP_RESPONSE_PDU: cmd_id: %d, qid: %d, time: %llu\n", cmd_id,
    //         qid, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, SETUP_RESPONSE_PDU, time);
    }
  }
}

void on_try_send_data_pdu(void* ignore, u16 cmd_id, int qid, int cp_len,
                          int left, unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info(
    //     "TRY_SEND_DATA_PDU: cmd_id: %d, qid: %d, cp_len: %d, left: %d, time:
    //     "
    //     "%llu\n",
    //     cmd_id, qid, cp_len, left, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, TRY_SEND_DATA_PDU, time);
    }
  }
}

void on_try_send_r2t(void* ignore, u16 cmd_id, int qid, int cp_len, int left,
                     unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info(
    //     "TRY_SEND_R2T: cmd_id: %d, qid: %d, cp_len: %d, left: %d, time: "
    //     "%llu\n",
    //     cmd_id, qid, cp_len, left, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, TRY_SEND_R2T, time);
    }
  }
}

bool is_standard_read(struct nvmet_io_instance* io_instance) {
  int i;
  if (io_instance->cnt < 8) {
    return false;
  }
  if (io_instance->trpt[0] != DONE_RECV_PDU ||
      io_instance->trpt[1] != EXEC_READ_REQ ||
      io_instance->trpt[2] != QUEUE_RESPONSE ||
      io_instance->trpt[3] != SETUP_C2H_DATA_PDU ||
      io_instance->trpt[4] != TRY_SEND_DATA_PDU ||
      io_instance->trpt[5] != TRY_SEND_DATA ||
      io_instance->trpt[io_instance->cnt - 1] != TRY_SEND_RESPONSE ||
      io_instance->trpt[io_instance->cnt - 2] != SETUP_RESPONSE_PDU) {
    return false;
  }
  for (i = 6; i < io_instance->cnt - 2; i++) {
    if (io_instance->trpt[i] != TRY_RECV_DATA) {
      return false;
    }
  }
  return true;
}

bool is_standard_write(struct nvmet_io_instance* io_instance) {
  int i;
  int cnt = io_instance->cnt;
  if (cnt < 6) {
    return false;
  }
  if (io_instance->contain_r2t) {
    if (io_instance->trpt[0] != DONE_RECV_PDU ||
        io_instance->trpt[1] != QUEUE_RESPONSE ||
        io_instance->trpt[2] != SETUP_R2T_PDU ||
        io_instance->trpt[3] != TRY_SEND_R2T ||
        io_instance->trpt[4] != HANDLE_H2C_DATA_PDU ||
        io_instance->trpt[5] != TRY_RECV_DATA ||
        io_instance->trpt[cnt - 4] != EXEC_WRITE_REQ ||
        io_instance->trpt[cnt - 3] != QUEUE_RESPONSE ||
        io_instance->trpt[cnt - 2] != SETUP_RESPONSE_PDU ||
        io_instance->trpt[cnt - 1] != TRY_SEND_RESPONSE) {
      return false;
    }
    for (i = 6; i < cnt - 4; i++) {
      if (io_instance->trpt[i] != TRY_RECV_DATA) {
        return false;
      }
    }
    return true;

  } else {
    if (io_instance->trpt[0] != DONE_RECV_PDU ||
        io_instance->trpt[1] != TRY_RECV_DATA ||
        io_instance->trpt[cnt - 4] != EXEC_WRITE_REQ ||
        io_instance->trpt[cnt - 3] != QUEUE_RESPONSE ||
        io_instance->trpt[cnt - 2] != SETUP_RESPONSE_PDU ||
        io_instance->trpt[cnt - 1] != TRY_SEND_RESPONSE) {
      return false;
    }
    for (i = 2; i < cnt - 4; i++) {
      if (io_instance->trpt[i] != TRY_RECV_DATA) {
        return false;
      }
    }
    return true;
  }
}

void update_read_breakdown(struct nvmet_tcp_read_breakdown* breakdown,
                           struct nvmet_io_instance* io_instance) {
  u64 in_blk_time = io_instance->ts[2] - io_instance->ts[1];
  u64 total = io_instance->ts[io_instance->cnt - 1] - io_instance->ts[0];
  breakdown->in_blk_time += in_blk_time;
  breakdown->in_nvmet_tcp_time += (total - in_blk_time);
  breakdown->end2end_time += total;
  breakdown->cnt++;
}

void update_write_breakdown(struct nvmet_tcp_write_breakdown* breakdown,
                            struct nvmet_io_instance* io_instance) {
  int cnt = io_instance->cnt;
  if (io_instance->contain_r2t) {
    u64 make_r2t_time = io_instance->ts[3] - io_instance->ts[0];
    u64 in_blk_time = io_instance->ts[cnt - 3] - io_instance->ts[cnt - 4];
    u64 total = io_instance->ts[cnt - 1] - io_instance->ts[0];
    breakdown->make_r2t_time += make_r2t_time;
    breakdown->in_blk_time += in_blk_time;
    breakdown->in_nvmet_tcp_time += (total - in_blk_time - make_r2t_time);
    breakdown->end2end_time += total;
    breakdown->cnt++;
  } else {
    u64 in_blk_time = io_instance->ts[cnt - 3] - io_instance->ts[cnt - 4];
    u64 total = io_instance->ts[cnt - 1] - io_instance->ts[0];
    breakdown->in_blk_time += in_blk_time;
    breakdown->in_nvmet_tcp_time += (total - in_blk_time);
    breakdown->end2end_time += total;
    breakdown->cnt++;
  }
}

void on_try_send_response(void* ignore, u16 cmd_id, int qid, int cp_len,
                          int left, unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info(
    //     "TRY_SEND_RESPONSE: cmd_id: %d, qid: %d, cp_len: %d, left: %d, time:
    //     "
    //     "%llu\n",
    //     cmd_id, qid, cp_len, left, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, TRY_SEND_RESPONSE, time);
      /** insert the current io sample to the sample sliding window */
      // print_io_instance(current_io);
      if (!current_io->is_spoiled) {
        struct sw_node* node;
        node = kmalloc(sizeof(struct sw_node), GFP_KERNEL);
        node->data = current_io;
        node->timestamp = current_io->ts[0];
        add_to_sliding_window(sw_nvmet_tcp_io_samples, node);

        if (current_io->is_write) {
          update_write_breakdown(&nvmettcp_stat->all_write, current_io);
          if (!is_standard_write(current_io)) {
            pr_err("write io is not standard: ");
            // print_io_instance(current_io);
          }
        } else {
          update_read_breakdown(&nvmettcp_stat->all_read, current_io);
          if (!is_standard_read(current_io)) {
            pr_err("read io is not standard: ");
            // print_io_instance(current_io);
          }
        }

        current_io = NULL;
      } else {
        pr_info("current_io is spoiled\n");
      }
    }
  }
}

void on_try_send_data(void* ignore, u16 cmd_id, int qid, int cp_len,
                      unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("TRY_SEND_DATA: cmd_id: %d, qid: %d, cp_len: %d, time: %llu\n",
    //         cmd_id, qid, cp_len, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, TRY_SEND_DATA, time);
    }
  }
}

void on_try_recv_data(void* ignore, u16 cmd_id, int qid, int cp_len,
                      unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info("TRY_RECV_DATA: cmd_id: %d, qid: %d, cp_len: %d, time: %llu\n",
    //         cmd_id, qid, cp_len, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, TRY_RECV_DATA, time);
    }
  }
}

void on_handle_h2c_data_pdu(void* ignore, u16 cmd_id, int qid, int datalen,
                            unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    // pr_info(
    //     "HANDLE_H2C_DATA_PDU: cmd_id: %d, qid: %d, datalen: %d, time:
    //     %llu\n", cmd_id, qid, datalen, time);
    if (current_io && current_io->command_id == cmd_id) {
      append_event(current_io, HANDLE_H2C_DATA_PDU, time);
    }
  }
}

void init_nvmet_tcp_read_breakdown(struct nvmet_tcp_read_breakdown* breakdown) {
  breakdown->in_nvmet_tcp_time = 0;
  breakdown->in_blk_time = 0;
  breakdown->cnt = 0;
  breakdown->end2end_time = 0;
}

void init_nvmet_tcp_write_breakdown(
    struct nvmet_tcp_write_breakdown* breakdown) {
  breakdown->make_r2t_time = 0;
  breakdown->in_nvmet_tcp_time = 0;
  breakdown->in_blk_time = 0;
  breakdown->cnt = 0;
  breakdown->end2end_time = 0;
}



void analyze_io_samples(void) {
  struct list_head *pos, *q;
  init_nvmet_tcp_read_breakdown(&nvmettcp_stat->sw_read_breakdown);
  init_nvmet_tcp_write_breakdown(&nvmettcp_stat->sw_write_breakdown);
  spin_lock(&sw_nvmet_tcp_io_samples->lock);
  list_for_each_safe(pos, q, &sw_nvmet_tcp_io_samples->list) {
    struct sw_node* node = list_entry(pos, struct sw_node, list);
    struct nvmet_io_instance* io_instance = node->data;
    if (io_instance->is_spoiled) {
      continue;
    }
    if (io_instance->is_write) {
      update_write_breakdown(&nvmettcp_stat->sw_write_breakdown, io_instance);
    } else {
      update_read_breakdown(&nvmettcp_stat->sw_read_breakdown, io_instance);
    }
  }

  spin_unlock(&sw_nvmet_tcp_io_samples->lock);
}

void nvmet_tcp_stat_update(u64 now) {
  /** TODO: analize the sample set */
  remove_from_sliding_window(sw_nvmet_tcp_io_samples, now - 10 * NSEC_PER_SEC);
  analyze_io_samples();
}

static int nvmet_tcp_register_tracepoints(void) {
  int ret;

  pr_info("register tracepoints\n");
  pr_info("register try_recv_pdu\n");
  ret = register_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL);
  if (ret) goto failed;
  pr_info("register done_recv_pdu\n");
  ret = register_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
  if (ret) goto unregister_try_recv_pdu;
  pr_info("register exec_read_req\n");
  ret = register_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
  if (ret) goto unregister_done_recv_pdu;
  pr_info("register exec_write_req\n");
  ret = register_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
  if (ret) goto unregister_exec_read_req;
  pr_info("register queue_response\n");
  ret = register_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
  if (ret) goto unregister_exec_write_req;
  pr_info("register setup_c2h_data_pdu\n");
  ret =
      register_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
  if (ret) goto unregister_queue_response;
  pr_info("register setup_r2t_pdu\n");
  ret = register_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
  if (ret) goto unregister_setup_c2h_data_pdu;
  pr_info("register setup_response_pdu\n");
  ret =
      register_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
  if (ret) goto unregister_setup_r2t_pdu;
  pr_info("register try_send_data_pdu\n");
  ret = register_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
  if (ret) goto unregister_setup_response_pdu;
  pr_info("register try_send_r2t\n");
  ret = register_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
  if (ret) goto unregister_try_send_data_pdu;
  pr_info("register try_send_response\n");
  ret = register_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
  if (ret) goto unregister_try_send_r2t;
  pr_info("register try_send_data\n");
  ret = register_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
  if (ret) goto unregister_try_send_response;
  pr_info("register handle_h2c_data_pdu\n");
  ret = register_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu,
                                                     NULL);
  if (ret) goto unregister_try_send_data;
  pr_info("register try_recv_data\n");
  ret = register_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
  if (ret) goto unregister_handle_h2c_data_pdu;

  return 0;

unregister_handle_h2c_data_pdu:
  unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
unregister_try_send_data:
  unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
unregister_try_send_response:
  unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
unregister_try_send_r2t:
  unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
unregister_try_send_data_pdu:
  unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
unregister_setup_response_pdu:
  unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
unregister_setup_r2t_pdu:
  unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
unregister_setup_c2h_data_pdu:
  unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
unregister_queue_response:
  unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
unregister_exec_write_req:
  unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
unregister_exec_read_req:
  unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
unregister_done_recv_pdu:
  unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
unregister_try_recv_pdu:
  unregister_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL);
failed:
  pr_info("failed to register tracepoints\n");
  return ret;
}

void nvmet_tcp_unregister_tracepoints(void) {
  pr_info("unregister tracepoints\n");
  unregister_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL);
  unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
  unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
  unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
  unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
  unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
  unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
  unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
  unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
  unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
  unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
  unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
  unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
  unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
}

static int mmap_nvmet_tcp_stat(struct file* file, struct vm_area_struct* vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(nvmettcp_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
    return -EAGAIN;
  }
  return 0;
}

static const struct proc_ops nttm_nvmet_tcp_stat_fops = {
    .proc_mmap = mmap_nvmet_tcp_stat,
};

int init_nvmet_tcp_proc_entries(void) {
  entry_nvmet_tcp_dir = proc_mkdir("nvmet_tcp", parent_dir);
  if (!entry_nvmet_tcp_dir) {
    pr_err("failed to create nvmet_tcp directory\n");
    return -ENOMEM;
  }
  entry_nvmet_tcp_stat =
      proc_create("stat", 0, entry_nvmet_tcp_dir, &nttm_nvmet_tcp_stat_fops);
  if (!entry_nvmet_tcp_stat) {
    pr_err("failed to create nvmet_tcp/stat\n");
    vfree(nvmettcp_stat);
    return -ENOMEM;
  }
  return 0;
}

static void remove_nvmet_tcp_proc_entries(void) {
  remove_proc_entry("stat", entry_nvmet_tcp_dir);
  remove_proc_entry("nvmet_tcp", parent_dir);
}

void reset_nvmet_tcp_sw_stat(struct nvmet_tcp_stat* stat) {
  init_nvmet_tcp_read_breakdown(&stat->sw_read_breakdown);
  init_nvmet_tcp_write_breakdown(&stat->sw_write_breakdown);
}

void init_nvmet_tcp_stat(struct nvmet_tcp_stat* stat) {
  reset_nvmet_tcp_sw_stat(stat);
  init_nvmet_tcp_read_breakdown(&stat->all_read);
  init_nvmet_tcp_write_breakdown(&stat->all_write);
}

int init_nvmet_tcp_variables(void) {
  // pr_info("try to allocate size %d\n", sizeof(*nvmettcp_stat));
  nvmettcp_stat = vmalloc(sizeof(*nvmettcp_stat));
  if (!nvmettcp_stat) {
    pr_err("failed to allocate nvmet_tcp_stat\n");
    return -ENOMEM;
  }
  init_nvmet_tcp_stat(nvmettcp_stat);

  sw_nvmet_tcp_io_samples = kmalloc(sizeof(struct sliding_window), GFP_KERNEL);
  init_sliding_window(sw_nvmet_tcp_io_samples);

  return 0;
}

void free_nvmet_tcp_variables(void) {
  if (nvmettcp_stat) vfree(nvmettcp_stat);
  if (sw_nvmet_tcp_io_samples) {
    // TODO: free the sliding window
    kfree(sw_nvmet_tcp_io_samples);
  }
}

int init_nvmet_tcp_layer(void) {
  int ret;
  pr_info("init nvmet tcp layer\n");
  ret = init_nvmet_tcp_variables();
  if (ret) return ret;
  ret = init_nvmet_tcp_proc_entries();
  if (ret) {
    pr_info("failed to init nvmet tcp proc entries\n");
    free_nvmet_tcp_variables();
  }
  ret = nvmet_tcp_register_tracepoints();
  if (ret) {
    pr_info("failed to register tracepoints\n");
    return ret;
  }
  return 0;
}

void exit_nvmet_tcp_layer(void) {
  remove_nvmet_tcp_proc_entries();
  free_nvmet_tcp_variables();
  nvmet_tcp_unregister_tracepoints();
  pr_info("exit nvmet tcp layer done\n");
}

#endif  // _K_NVME_TCP_LAYER_H_