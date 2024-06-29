#include "k_tcp_layer.h"

#include <trace/events/tcp_m.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include "k_ntm.h"
#include "ntm_com.h"

static atomic64_t sample_cnt;

struct tcp_stat *tcp_s;
struct atomic_tcp_stat *raw_tcp_stat;

struct proc_dir_entry *entry_tcp_dir;
struct proc_dir_entry *entry_tcp_stat;
static char *dir_name = "tcp";
static char *stat_name = "stat";

static bool to_sample(void) {
  return atomic64_inc_return(&sample_cnt) % args->nrate == 0;
}

void init_raw_tcp_stat(struct atomic_tcp_stat *stat) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    atomic_set(&stat->sks[i].pkt_in_flight, 0);
    atomic_set(&stat->sks[i].cwnd, 0);
    spin_lock_init(&stat->sks[i].lock);
    stat->sks[i].last_event[0] = '\0';
  }
}

/** this is a filter */
bool remote_port_filter(int port) { return port == 4420; }

bool local_port_filter(int port_id) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    if (qid2port[i] == port_id) {
      return true;
    }
  }
  return false;
}

int port2qid(int port) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    if (qid2port[i] == port) {
      return i;
    }
  }
  return -1;
}

void on_cwnd_tcp_slow_start(void *ignore, u32 old_cwnd, u32 new_cwnd,
                            u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_slow_start");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_cong_avoid_ai(void *ignore, u32 old_cwnd, u32 new_cwnd,
                               u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_cong_avoid_ai");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_enter_loss(void *ignore, u32 old_cwnd, u32 new_cwnd,
                            u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_enter_loss");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_undo_cwnd_reduction(void *ignore, u32 old_cwnd, u32 new_cwnd,
                                     u32 local_port, u32 remote_port,
                                     u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_undo_cwnd_reduction");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_cwnd_reduction(void *ignore, u32 old_cwnd, u32 new_cwnd,
                                u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_cwnd_reduction");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_end_cwnd_reduction(void *ignore, u32 old_cwnd, u32 new_cwnd,
                                    u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_end_cwnd_reduction");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_mtup_probe_success(void *ignore, u32 old_cwnd, u32 new_cwnd,
                                    u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_mtup_probe_success");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_mtup_probe_failed(void *ignore, u32 old_cwnd, u32 new_cwnd,
                                   u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_mtup_probe_failed");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_init_transfer(void *ignore, u32 old_cwnd, u32 new_cwnd,
                               u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_init_transfer");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_cwnd_restart(void *ignore, u32 old_cwnd, u32 new_cwnd,
                              u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_cwnd_restart");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_cwnd_application_limited(void *ignore, u32 old_cwnd,
                                          u32 new_cwnd, u32 local_port,
                                          u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_cwnd_application_limited");
    spin_unlock(&p->lock);
  }
}

void on_cwnd_tcp_mtu_probe(void *ignore, u32 old_cwnd, u32 new_cwnd,
                           u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port)) {
    pr_info("tcp_slow_start, old_cwnd: %d, new_cwnd: %d, local_port: %d, remote_port: %d\n",
            old_cwnd, new_cwnd, local_port, remote_port);
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->cwnd, new_cwnd);
    spin_lock(&p->lock);
    snprintf(p->last_event, 64, "tcp_mtu_probe");
    spin_unlock(&p->lock);
  }
}

void on_pkt_tcp_event_new_data_sent(void *ignore, u32 packeg_in_flight,
                                    u32 cwnd, u32 local_port, u32 remote_port,
                                    u64 time) {

  if (remote_port_filter(remote_port) && port2qid(local_port) != -1 && to_sample() ){
  //     pr_info("data send, pktout: %d, cwnd: %d, local: %d, remot: %d\n",
  // packeg_in_flight, cwnd, local_port, remote_port);
  

    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->pkt_in_flight, packeg_in_flight);
    atomic_set(&p->cwnd, cwnd);
  }
}

void on_pkt_tcp_connect_queue_skb(void *ignore, u32 packeg_in_flight, u32 cwnd,
                                  u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port) &&
      to_sample()) {
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->pkt_in_flight, packeg_in_flight);
    atomic_set(&p->cwnd, cwnd);
  }
}

void on_pkt_tcp_clean_rtx_queue(void *ignore, u32 packeg_in_flight, u32 cwnd,
                                u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port) &&
      to_sample()) {
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->pkt_in_flight, packeg_in_flight);
    atomic_set(&p->cwnd, cwnd);
  }
}

void on_pkt_tcp_adjust_pcount(void *ignore, u32 packeg_in_flight, u32 cwnd,
                              u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port) &&
      to_sample()) {
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->pkt_in_flight, packeg_in_flight);
    atomic_set(&p->cwnd, cwnd);
  }
}

void on_pkt_tcp_send_syn_data(void *ignore, u32 packeg_in_flight, u32 cwnd,
                              u32 local_port, u32 remote_port, u64 time) {
  if (remote_port_filter(remote_port) && local_port_filter(local_port) &&
      to_sample()) {
    struct atomic_tcp_stat_of_one_queue *p = &raw_tcp_stat->sks[port2qid(local_port)];
    atomic_set(&p->pkt_in_flight, packeg_in_flight);
    atomic_set(&p->cwnd, cwnd);
  }
}

void copy_tcp_stat(struct tcp_stat *dst, struct atomic_tcp_stat *src) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    dst->sks[i].pkt_in_flight = atomic_read(&src->sks[i].pkt_in_flight);
    dst->sks[i].cwnd = atomic_read(&src->sks[i].cwnd);
    spin_lock(&src->sks[i].lock);
    strcpy(dst->sks[i].last_event, src->sks[i].last_event);
    spin_unlock(&src->sks[i].lock);
  }
  smp_mb();
}

void tcp_stat_update(void) {
  /** copy the attributes from raw to the shared */
  copy_tcp_stat(tcp_s, raw_tcp_stat);
  

  // int i;
  // for(i = 55; i < MAX_QID; i++) {
  //   pr_info("copy tcp_stat [%d] in-flight: %d, cwnd: %d\n", i, tcp_s->sks[i].pkt_in_flight, tcp_s->sks[i].cwnd);
  // }
}

static int tcp_register_tracepoints(void) {
  int ret;
  ret = register_trace_cwnd_tcp_slow_start(on_cwnd_tcp_slow_start, NULL);
  if (ret) goto failed;
  ret = register_trace_cwnd_tcp_cong_avoid_ai(on_cwnd_tcp_cong_avoid_ai, NULL);
  if (ret) goto unregister_cwnd_tcp_slow_start;
  ret = register_trace_cwnd_tcp_enter_loss(on_cwnd_tcp_enter_loss, NULL);
  if (ret) goto unregister_cwnd_tcp_cong_avoid_ai;
  ret = register_trace_cwnd_tcp_undo_cwnd_reduction(
      on_cwnd_tcp_undo_cwnd_reduction, NULL);
  if (ret) goto unregister_cwnd_tcp_enter_loss;
  ret =
      register_trace_cwnd_tcp_cwnd_reduction(on_cwnd_tcp_cwnd_reduction, NULL);
  if (ret) goto unregister_cwnd_tcp_undo_cwnd_reduction;
  ret = register_trace_cwnd_tcp_end_cwnd_reduction(
      on_cwnd_tcp_end_cwnd_reduction, NULL);
  if (ret) goto unregister_cwnd_tcp_cwnd_reduction;
  ret = register_trace_cwnd_tcp_mtup_probe_success(
      on_cwnd_tcp_mtup_probe_success, NULL);
  if (ret) goto unregister_cwnd_tcp_end_cwnd_reduction;
  ret = register_trace_cwnd_tcp_mtup_probe_failed(on_cwnd_tcp_mtup_probe_failed,
                                                  NULL);
  if (ret) goto unregister_cwnd_tcp_mtup_probe_success;
  ret = register_trace_cwnd_tcp_init_transfer(on_cwnd_tcp_init_transfer, NULL);
  if (ret) goto unregister_cwnd_tcp_mtup_probe_failed;
  ret = register_trace_cwnd_tcp_cwnd_restart(on_cwnd_tcp_cwnd_restart, NULL);
  if (ret) goto unregister_cwnd_tcp_init_transfer;
  ret = register_trace_cwnd_tcp_cwnd_application_limited(
      on_cwnd_tcp_cwnd_application_limited, NULL);
  if (ret) goto unregister_cwnd_tcp_cwnd_restart;
  ret = register_trace_cwnd_tcp_mtu_probe(on_cwnd_tcp_mtu_probe, NULL);
  if (ret) goto unregister_cwnd_tcp_cwnd_application_limited;
  ret = register_trace_pkt_tcp_event_new_data_sent(
      on_pkt_tcp_event_new_data_sent, NULL);
  if (ret) goto unregister_cwnd_tcp_mtu_probe;
  ret = register_trace_pkt_tcp_connect_queue_skb(on_pkt_tcp_connect_queue_skb,
                                                 NULL);
  if (ret) goto unregister_pkt_tcp_event_new_data_sent;
  ret =
      register_trace_pkt_tcp_clean_rtx_queue(on_pkt_tcp_clean_rtx_queue, NULL);
  if (ret) goto unregister_pkt_tcp_connect_queue_skb;
  ret = register_trace_pkt_tcp_adjust_pcount(on_pkt_tcp_adjust_pcount, NULL);
  if (ret) goto unregister_pkt_tcp_clean_rtx_queue;
  ret = register_trace_pkt_tcp_send_syn_data(on_pkt_tcp_send_syn_data, NULL);
  if (ret) goto unregister_pkt_tcp_adjust_pcount;

  return 0;
unregister_pkt_tcp_adjust_pcount:
  unregister_trace_pkt_tcp_adjust_pcount(on_pkt_tcp_adjust_pcount, NULL);
unregister_pkt_tcp_clean_rtx_queue:
  unregister_trace_pkt_tcp_clean_rtx_queue(on_pkt_tcp_clean_rtx_queue, NULL);
unregister_pkt_tcp_connect_queue_skb:
  unregister_trace_pkt_tcp_connect_queue_skb(on_pkt_tcp_connect_queue_skb,
                                             NULL);
unregister_pkt_tcp_event_new_data_sent:
  unregister_trace_pkt_tcp_event_new_data_sent(on_pkt_tcp_event_new_data_sent,
                                               NULL);
unregister_cwnd_tcp_mtu_probe:
  unregister_trace_cwnd_tcp_mtu_probe(on_cwnd_tcp_mtu_probe, NULL);
unregister_cwnd_tcp_cwnd_application_limited:
  unregister_trace_cwnd_tcp_cwnd_application_limited(
      on_cwnd_tcp_cwnd_application_limited, NULL);
unregister_cwnd_tcp_cwnd_restart:
  unregister_trace_cwnd_tcp_cwnd_restart(on_cwnd_tcp_cwnd_restart, NULL);
unregister_cwnd_tcp_init_transfer:
  unregister_trace_cwnd_tcp_init_transfer(on_cwnd_tcp_init_transfer, NULL);
unregister_cwnd_tcp_mtup_probe_failed:
  unregister_trace_cwnd_tcp_mtup_probe_failed(on_cwnd_tcp_mtup_probe_failed,
                                              NULL);
unregister_cwnd_tcp_mtup_probe_success:
  unregister_trace_cwnd_tcp_mtup_probe_success(on_cwnd_tcp_mtup_probe_success,
                                               NULL);
unregister_cwnd_tcp_end_cwnd_reduction:
  unregister_trace_cwnd_tcp_end_cwnd_reduction(on_cwnd_tcp_end_cwnd_reduction,
                                               NULL);
unregister_cwnd_tcp_cwnd_reduction:
  unregister_trace_cwnd_tcp_cwnd_reduction(on_cwnd_tcp_cwnd_reduction, NULL);
unregister_cwnd_tcp_undo_cwnd_reduction:
  unregister_trace_cwnd_tcp_undo_cwnd_reduction(on_cwnd_tcp_undo_cwnd_reduction,
                                                NULL);
unregister_cwnd_tcp_enter_loss:
  unregister_trace_cwnd_tcp_enter_loss(on_cwnd_tcp_enter_loss, NULL);
unregister_cwnd_tcp_cong_avoid_ai:
  unregister_trace_cwnd_tcp_cong_avoid_ai(on_cwnd_tcp_cong_avoid_ai, NULL);
unregister_cwnd_tcp_slow_start:
  unregister_trace_cwnd_tcp_slow_start(on_cwnd_tcp_slow_start, NULL);
failed:
  return ret;
}

void tcp_unregister_tracepoints(void) {
  unregister_trace_cwnd_tcp_slow_start(on_cwnd_tcp_slow_start, NULL);
  unregister_trace_cwnd_tcp_cong_avoid_ai(on_cwnd_tcp_cong_avoid_ai, NULL);
  unregister_trace_cwnd_tcp_enter_loss(on_cwnd_tcp_enter_loss, NULL);
  unregister_trace_cwnd_tcp_undo_cwnd_reduction(on_cwnd_tcp_undo_cwnd_reduction,
                                                NULL);
  unregister_trace_cwnd_tcp_cwnd_reduction(on_cwnd_tcp_cwnd_reduction, NULL);
  unregister_trace_cwnd_tcp_end_cwnd_reduction(on_cwnd_tcp_end_cwnd_reduction,
                                               NULL);
  unregister_trace_cwnd_tcp_mtup_probe_success(on_cwnd_tcp_mtup_probe_success,
                                               NULL);
  unregister_trace_cwnd_tcp_mtup_probe_failed(on_cwnd_tcp_mtup_probe_failed,
                                              NULL);
  unregister_trace_cwnd_tcp_init_transfer(on_cwnd_tcp_init_transfer, NULL);
  unregister_trace_cwnd_tcp_cwnd_restart(on_cwnd_tcp_cwnd_restart, NULL);
  unregister_trace_cwnd_tcp_cwnd_application_limited(
      on_cwnd_tcp_cwnd_application_limited, NULL);
  unregister_trace_cwnd_tcp_mtu_probe(on_cwnd_tcp_mtu_probe, NULL);
  unregister_trace_pkt_tcp_event_new_data_sent(on_pkt_tcp_event_new_data_sent,
                                               NULL);
  unregister_trace_pkt_tcp_connect_queue_skb(on_pkt_tcp_connect_queue_skb,
                                             NULL);
  unregister_trace_pkt_tcp_clean_rtx_queue(on_pkt_tcp_clean_rtx_queue, NULL);
  unregister_trace_pkt_tcp_adjust_pcount(on_pkt_tcp_adjust_pcount, NULL);
  unregister_trace_pkt_tcp_send_syn_data(on_pkt_tcp_send_syn_data, NULL);
}

static int mmap_tcp_stat(struct file *file, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(tcp_s),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
    return -EAGAIN;
  }
  return 0;
}

static const struct proc_ops ntm_tcp_stat_fops = {
    .proc_mmap = mmap_tcp_stat,
};

int init_tcp_proc_entries(void) {
  entry_tcp_dir = proc_mkdir(dir_name, parent_dir);
  if (!entry_tcp_dir) {
    pr_err("Failed to create %s\n", dir_name);
    return -ENOMEM;
  }
  entry_tcp_stat = proc_create(stat_name, 0, entry_tcp_dir, &ntm_tcp_stat_fops);
  if (!entry_tcp_stat) {
    pr_err("Failed to create %s/%s\n", dir_name, stat_name);
    vfree(tcp_s);
    return -ENOMEM;
  }
  return 0;
}

static void remove_tcp_proc_entries(void) {
  remove_proc_entry("stat", entry_tcp_dir);
  remove_proc_entry("tcp", parent_dir);
}

int init_tcp_variables(void) {
  atomic64_set(&sample_cnt, 0);
  tcp_s = vmalloc(sizeof(struct tcp_stat));
  if (!tcp_s) {
    pr_err("Failed to allocate memory for tcp_stat\n");
    return -ENOMEM;
  }
  init_tcp_stat(tcp_s);

  raw_tcp_stat = kmalloc(sizeof(struct atomic_tcp_stat), GFP_KERNEL);
  if (!raw_tcp_stat) {
    pr_err("Failed to allocate memory for raw_tcp_stat\n");
    vfree(tcp_s);
    return -ENOMEM;
  }
  init_raw_tcp_stat(raw_tcp_stat);

  return 0;
}

void free_tcp_variables(void) {
  if (tcp_s) {
    vfree(tcp_s);
  }
  if (raw_tcp_stat) {
    kfree(raw_tcp_stat);
  }
}

int init_tcp_layer(void) {
  int ret;
  pr_info("Initializing TCP layer\n");
  ret = init_tcp_variables();
  if (ret) {
    return ret;
  }
  ret = init_tcp_proc_entries();
  if (ret) {
    pr_info("Failed to create proc entries\n");
    free_tcp_variables();
    return ret;
  }
  ret = tcp_register_tracepoints();
  if (ret) {
    pr_info("Failed to register tracepoints\n");
    free_tcp_variables();
    return ret;
  }
  return 0;
}

void exit_tcp_layer(void) {
  pr_info("Exiting TCP layer\n");
  remove_tcp_proc_entries();
  free_tcp_variables();
  tcp_unregister_tracepoints();
}
