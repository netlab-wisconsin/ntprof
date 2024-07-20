#include "k_nvmet_tcp_layer.h"

#include <linux/irqflags.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
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

#define timethred 7000
#define to_trace \
  (current_io && current_io->command_id == cmd_id && current_io->qid == qid)

static atomic64_t sample_cnt;
static atomic64_t io_work_sample_cnt;

// static struct mutex current_io_lock;
// static spinlock_t current_io_lock;
static struct nvmet_io_instance* current_io = NULL;

struct proc_dir_entry* entry_nvmet_tcp_dir;
struct proc_dir_entry* entry_nvmet_tcp_stat;

/** TODO: make atomic type for this */
struct nvmet_tcp_stat* nvmettcp_stat;
struct atomic_nvmet_tcp_stat* atomic_nvmettcp_stat;

static bool to_sample(void) {
  // return atomic64_inc_return(&sample_cnt) % args->rate == 0;
  return true;
}

static bool to_sample_io_work(void) {
  return atomic64_inc_return(&io_work_sample_cnt) % args->rate == 0;
}

void append_event(struct nvmet_io_instance* io_instance,
                  enum nvmet_tcp_trpt trpt, u64 ts, long long recv_time) {
  if (io_instance->cnt < EVENT_NUM) {
    io_instance->trpt[io_instance->cnt] = trpt;
    io_instance->ts[io_instance->cnt] = ts;
    io_instance->recv_ts[io_instance->cnt] = recv_time;
    io_instance->cnt++;
  } else {
    io_instance->is_spoiled = true;
  }
}

void on_try_recv_pdu(void* ignore, u8 pdu_type, u8 hdr_len, int queue_left,
                     int qid, int remote_port, unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (qid2port[qid] == -1) {
      pr_info("set qid2port[%d] = %d\n", qid, remote_port);
      qid2port[qid] = remote_port;
    }
  }
}

void on_done_recv_pdu(void* ignore, u16 cmd_id, int qid, bool is_write,
                      int size, unsigned long long time, long long recv_time) {
  if (ctrl && args->qid[qid] && args->io_type + is_write != 1) {
    // pr_info("%d, 0, %llu;", cmd_id, recv_time);
    if (to_sample()) {
      if (!current_io) {
        current_io = kmalloc(sizeof(struct nvmet_io_instance), GFP_KERNEL);

        init_nvmet_tcp_io_instance(current_io, cmd_id, is_write, size);
        append_event(current_io, DONE_RECV_PDU, time, recv_time);
        current_io->qid = qid;
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
    if (to_trace) {
      append_event(current_io, EXEC_READ_REQ, time, 0);
    }
  }
}

void on_exec_write_req(void* ignore, u16 cmd_id, int qid, bool is_write,
                       unsigned long long time) {
  if (qid == 0) return;
  if (!is_write) {
    pr_err("exec_write_req: is_write is false\n");
  }
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, EXEC_WRITE_REQ, time, 0);
    }
  }
}

void on_queue_response(void* ignore, u16 cmd_id, int qid, bool is_write,
                       unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, QUEUE_RESPONSE, time, 0);
    }
  }
}

void on_setup_c2h_data_pdu(void* ignore, u16 cmd_id, int qid,
                           unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, SETUP_C2H_DATA_PDU, time, 0);
    }
  }
}

void on_setup_r2t_pdu(void* ignore, u16 cmd_id, int qid,
                      unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      current_io->contain_r2t = true;
      append_event(current_io, SETUP_R2T_PDU, time, 0);
    }
  }
}

void on_setup_response_pdu(void* ignore, u16 cmd_id, int qid,
                           unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, SETUP_RESPONSE_PDU, time, 0);
    }
  }
}

void on_try_send_data_pdu(void* ignore, u16 cmd_id, int qid, int cp_len,
                          int left, unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, TRY_SEND_DATA_PDU, time, 0);
    }
  }
}

void on_try_send_r2t(void* ignore, u16 cmd_id, int qid, int cp_len, int left,
                     unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, TRY_SEND_R2T, time, 0);
    }
  }
}

u64 last_time;
int cmd_num;
void on_nvmet_tcp_recv_msg_types(void* ignore, int* cnt, int qid, u64 ts) {
  // if (ctrl && args->qid[qid]) {
  //   if (cnt[nvme_tcp_cmd]) {
  //     if (ts - last_time < args->latency_group_thred) {
  //       cmd_num += cnt[nvme_tcp_cmd];
  //     } else {
  //       if (cmd_num > MAX_BATCH_SIZE) {
  //         pr_err("cmd number in a batch is too large: %d\n", cmd_num);
  //       } else {
  //         if (cmd_num) nvmettcp_stat->batch_size_hist[cmd_num]++;
  //       }
  //       cmd_num = cnt[nvme_tcp_cmd];
  //     }
  //   }
  //   last_time = ts;
  // }
}

bool is_valid_read(struct nvmet_io_instance* io_instance) {
  bool ret;
  int i;

  if (io_instance->cnt < 8) {
    return false;
  }
  ret = true;
  ret = ret && io_instance->trpt[0] == DONE_RECV_PDU;
  ret = ret && io_instance->trpt[1] == EXEC_READ_REQ;
  ret = ret && io_instance->trpt[2] == QUEUE_RESPONSE;
  ret = ret && io_instance->trpt[3] == SETUP_C2H_DATA_PDU;
  ret = ret && io_instance->trpt[4] == TRY_SEND_DATA_PDU;

  for (i = 5; i < io_instance->cnt - 2; i++) {
    ret = ret && io_instance->trpt[i] == TRY_SEND_DATA;
  }
  ret = ret && io_instance->trpt[io_instance->cnt - 2] == SETUP_RESPONSE_PDU;
  ret = ret && io_instance->trpt[io_instance->cnt - 1] == TRY_SEND_RESPONSE;
  return ret;
}

bool is_valid_write(struct nvmet_io_instance* io_instance, bool contain_rt2) {
  if (contain_rt2) {
    bool ret;
    int i;
    if (io_instance->cnt < 10) return false;
    ret = true;
    ret = ret && io_instance->trpt[0] == DONE_RECV_PDU;
    ret = ret && io_instance->trpt[1] == QUEUE_RESPONSE;
    ret = ret && io_instance->trpt[2] == SETUP_R2T_PDU;
    ret = ret && io_instance->trpt[3] == TRY_SEND_R2T;
    ret = ret && io_instance->trpt[4] == HANDLE_H2C_DATA_PDU;

    for (i = 5; i < io_instance->cnt - 4; i++) {
      ret = ret && io_instance->trpt[i] == TRY_RECV_DATA;
    }
    ret = ret && io_instance->trpt[io_instance->cnt - 4] == EXEC_WRITE_REQ;
    ret = ret && io_instance->trpt[io_instance->cnt - 3] == QUEUE_RESPONSE;
    ret = ret && io_instance->trpt[io_instance->cnt - 2] == SETUP_RESPONSE_PDU;
    ret = ret && io_instance->trpt[io_instance->cnt - 1] == TRY_SEND_RESPONSE;
    return ret;
  } else {
    bool ret;
    int i;
    if (io_instance->cnt < 6) return false;
    ret = true;
    ret = ret && io_instance->trpt[0] == DONE_RECV_PDU;
    for (i = 1; i < io_instance->cnt - 4; i++) {
      ret = ret && io_instance->trpt[i] == TRY_RECV_DATA;
    }
    ret = ret && io_instance->trpt[io_instance->cnt - 4] == EXEC_WRITE_REQ;
    ret = ret && io_instance->trpt[io_instance->cnt - 3] == QUEUE_RESPONSE;
    ret = ret && io_instance->trpt[io_instance->cnt - 2] == SETUP_RESPONSE_PDU;
    ret = ret && io_instance->trpt[io_instance->cnt - 1] == TRY_SEND_RESPONSE;
    return ret;
  }
}

void update_atomic_read_breakdown(
    struct atomic_nvmet_tcp_read_breakdown* breakdown,
    struct nvmet_io_instance* io_instance) {
  u64 cmd_caps_q = io_instance->ts[0] - io_instance->recv_ts[0];
  u64 cmd_proc = io_instance->ts[1] - io_instance->ts[0];
  u64 sub_and_exec = io_instance->ts[2] - io_instance->ts[1];
  u64 comp_q = io_instance->ts[3] - io_instance->ts[2];
  u64 resp_proc = io_instance->ts[io_instance->cnt - 1] - io_instance->ts[3];

  atomic64_add(cmd_caps_q, &breakdown->cmd_caps_q);
  atomic64_add(cmd_proc, &breakdown->cmd_proc);
  atomic64_add(sub_and_exec, &breakdown->sub_and_exec);
  atomic64_add(comp_q, &breakdown->comp_q);
  atomic64_add(resp_proc, &breakdown->resp_proc);
  atomic64_add(io_instance->ts[io_instance->cnt - 1] - io_instance->recv_ts[0],
               &breakdown->end2end);
  atomic_inc(&breakdown->cnt);
}

void update_atomic_write_breakdown(
    struct atomic_nvmet_tcp_write_breakdown* breakdown,
    struct nvmet_io_instance* io_instance) {
  int cnt = io_instance->cnt;
  if (io_instance->contain_r2t) {
    atomic64_add(io_instance->ts[0] - io_instance->recv_ts[0],
                 &breakdown->cmd_caps_q);
    atomic64_add(io_instance->ts[1] - io_instance->ts[0],
                 &breakdown->cmd_proc);  // process cmd, create r2t
    atomic64_add(io_instance->ts[2] - io_instance->ts[1],
                 &breakdown->r2t_sub_q);
    atomic64_add(io_instance->ts[3] - io_instance->ts[2],
                 &breakdown->r2t_resp_proc);
    atomic64_add(io_instance->recv_ts[4] - io_instance->ts[3],
                 &breakdown->wait_for_data);
    atomic64_add(io_instance->ts[4] - io_instance->recv_ts[4],
                 &breakdown->write_cmd_q);
    atomic64_add(io_instance->ts[cnt - 4] - io_instance->ts[4],
                 &breakdown->write_cmd_proc);
    atomic64_add(io_instance->ts[cnt - 3] - io_instance->ts[cnt - 4],
                 &breakdown->nvme_sub_exec);
    atomic64_add(io_instance->ts[cnt - 2] - io_instance->ts[cnt - 3],
                 &breakdown->comp_q);
    atomic64_add(io_instance->ts[cnt - 1] - io_instance->ts[cnt - 2],
                 &breakdown->resp_proc);
    atomic64_add(io_instance->ts[cnt - 1] - io_instance->recv_ts[0],
                 &breakdown->e2e);
    atomic_inc(&breakdown->cnt);

  } else {
    atomic64_add(io_instance->ts[0] - io_instance->recv_ts[0],
                 &breakdown->cmd_caps_q);
    atomic64_add(io_instance->ts[cnt - 4] - io_instance->ts[0],
                 &breakdown->write_cmd_proc);
    atomic64_add(io_instance->ts[cnt - 3] - io_instance->ts[cnt - 4],
                 &breakdown->nvme_sub_exec);
    atomic64_add(io_instance->ts[cnt - 2] - io_instance->ts[cnt - 3],
                 &breakdown->comp_q);
    atomic64_add(io_instance->ts[cnt - 1] - io_instance->ts[cnt - 2],
                 &breakdown->resp_proc);
    atomic64_add(io_instance->ts[cnt - 1] - io_instance->recv_ts[0],
                 &breakdown->e2e);
    atomic_inc(&breakdown->cnt);
  }
}

void on_try_send_response(void* ignore, u16 cmd_id, int qid, int cp_len,
                          int left, int is_write, unsigned long long time) {
  if (ctrl && args->qid[qid]) {
    if (is_write + args->io_type == 1) return;
    // pr_info("%d, 1, %llu;", cmd_id, time);
    if (to_trace) {
      append_event(current_io, TRY_SEND_RESPONSE, time, 0);
      /** insert the current io sample to the sample sliding window */
      if (args->detail) print_io_instance(current_io);
      if (!current_io->is_spoiled) {
        // pr_info("size of current req is %d\n", current_io->size);
        if (current_io->is_write) {
          if (is_valid_write(current_io, current_io->contain_r2t)) {
            update_atomic_write_breakdown(
                &atomic_nvmettcp_stat
                     ->write_breakdown[size_to_enum(current_io->size)],
                current_io);
          } else {
            pr_err("write io is not standard: ");
            print_io_instance(current_io);
          }
        } else {
          if (is_valid_read(current_io)) {
            update_atomic_read_breakdown(
                &atomic_nvmettcp_stat
                     ->read_breakdown[size_to_enum(current_io->size)],
                current_io);
          } else {
            pr_err("read io is not standard: ");
            print_io_instance(current_io);
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
    if (to_trace) {
      append_event(current_io, TRY_SEND_DATA, time, 0);
    }
  }
}

void on_try_recv_data(void* ignore, u16 cmd_id, int qid, int cp_len,
                      unsigned long long time, long long recv_time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      append_event(current_io, TRY_RECV_DATA, time, recv_time);
    }
  }
}

void on_handle_h2c_data_pdu(void* ignore, u16 cmd_id, int qid, int datalen,
                            unsigned long long time, long long recv_time) {
  if (ctrl && args->qid[qid]) {
    if (to_trace) {
      current_io->size = datalen;
      append_event(current_io, HANDLE_H2C_DATA_PDU, time, recv_time);
    }
  }
}

void on_io_work(void* ignore, int qid, long long recv, long long send) {
  if (ctrl && args->qid[qid] && to_sample_io_work()) {
    if (recv) {
      atomic64_add(recv, &atomic_nvmettcp_stat->recv);
      atomic64_inc(&atomic_nvmettcp_stat->recv_cnt);
    }
    if (send) {
      atomic64_add(send, &atomic_nvmettcp_stat->send);
      atomic64_inc(&atomic_nvmettcp_stat->send_cnt);
    }
  }
}

void nvmet_tcp_stat_update(u64 now) {
  copy_nvmet_tcp_stat(nvmettcp_stat, atomic_nvmettcp_stat);
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
  pr_info("register io_work\n");
  ret = register_trace_nvmet_tcp_io_work(on_io_work, NULL);
  if (ret) goto unregister_try_recv_data;
  ret = register_trace_nvmet_tcp_recv_msg_types(on_nvmet_tcp_recv_msg_types,
                                                NULL);
  if (ret) goto unregister_io_work;

  return 0;
unregister_io_work:
  unregister_trace_nvmet_tcp_io_work(on_io_work, NULL);
unregister_try_recv_data:
  unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
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
  unregister_trace_nvmet_tcp_io_work(on_io_work, NULL);
  unregister_trace_nvmet_tcp_recv_msg_types(on_nvmet_tcp_recv_msg_types, NULL);
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

int init_nvmet_tcp_variables(void) {
  // spin_lock_init(&current_io_lock);
  // mutex_init(&current_io_lock);
  last_time = 0;
  cmd_num = 0;
  atomic64_set(&sample_cnt, 0);
  atomic_nvmettcp_stat = kmalloc(sizeof(*atomic_nvmettcp_stat), GFP_KERNEL);
  init_atomic_nvmet_tcp_stat(atomic_nvmettcp_stat);

  nvmettcp_stat = vmalloc(sizeof(*nvmettcp_stat));
  if (!nvmettcp_stat) {
    pr_err("failed to allocate nvmet_tcp_stat\n");
    return -ENOMEM;
  }
  init_nvmet_tcp_stat(nvmettcp_stat);

  return 0;
}

void free_nvmet_tcp_variables(void) {
  if (atomic_nvmettcp_stat) kfree(atomic_nvmettcp_stat);
  if (nvmettcp_stat) vfree(nvmettcp_stat);
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