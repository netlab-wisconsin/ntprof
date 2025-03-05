#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/nvme-tcp.h>
#include <linux/nvme.h>
#include <linux/proc_fs.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>
#include <linux/timekeeping.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include "../include/statistics.h"
#include "host.h"
#include "host_logging.h"
#include "../include/trace.h"
#include <linux/delay.h>

#include "breakdown.h"


#define CHECK_TRACE_CONDITIONS(qid) \
    do { \
        if (unlikely(atomic_read(&trace_on) == 0 || (qid) == 0)) \
            return; \
    } while (0)

// #define LOCK(cid) \
//     SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, __func__)
//
// #define UNLOCK(cid) \
//     SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, __func__)



#define CHECK_FREQUENCY(cid) \
    do { \
        if (unlikely(global_config.frequency == 0)) { \
            pr_err_once("Sampling frequency is 0\n"); \
            return; \
        } \
        if (++stat[cid].sampler % global_config.frequency != 0) \
            return; \
    } while (0)


static struct profile_record* create_profile_record(
    struct request* req, int cid) {
  struct profile_record* record = kmalloc(sizeof(*record), GFP_KERNEL);
  if (unlikely(!record)) {
    pr_err("Allocation failed for tag %d\n", req->tag);
    return NULL;
  }

  init_profile_record(record,
                      blk_rq_bytes(req),
                      rq_data_dir(req),
                      req->rq_disk ? req->rq_disk->disk_name : "unknown",
                      req->tag);

  record->metadata.req = req;
  record->metadata.cmdid = -1;
  return record;
  // return NULL;
}

static inline u64 get_start_time(struct request* req) {
  return ktime_get_real_ns() - ktime_get_ns() + req->start_time_ns;
}


void on_nvme_tcp_queue_rq(void* ignore, struct request* req, void* pdu, int qid,
                          struct llist_head* req_list,
                          struct list_head* send_list,
                          struct mutex* send_mutex) {
  CHECK_TRACE_CONDITIONS(qid);
  u64 now = ktime_get_real_ns();

  int cid = smp_processor_id();
  CHECK_FREQUENCY(cid);

  if (!match_config(req, &global_config)) {
    ((struct nvme_tcp_cmd_pdu*)pdu)->stat.tag = false;
    return;
  }

  LOCKQ(qid);
  struct profile_record* record = create_profile_record(req, cid);
  if (record) {
    append_event(record, get_start_time(req), BLK_SUBMIT);
    append_event(record, now, NVME_TCP_QUEUE_RQ);
    append_record(&stat[qid], record);
    ((struct nvme_tcp_cmd_pdu*)pdu)->stat.tag = true;
  }
  UNLOCKQ(qid);
}

// void on_nvme_tcp_queue_request(void* ignore, struct request* req,
//                                struct nvme_command* cmd, int qid) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   if (rec) {
//     append_event(rec, now, NVME_TCP_QUEUE_REQUEST);
//     if (rec->metadata.cmdid == -1)
//       rec->metadata.cmdid = cmd->common.command_id;
//   }
//   UNLOCKQ(qid);
// }

#define APPEND_EVENT_IF_VALID(isValidExpr, rec, time, e) \
    do { \
        if (rec && (isValidExpr)) { \
            append_event(rec, time, e); \
        } \
    } while (0)


// void on_nvme_tcp_try_send(void* ignore, struct request* req, int qid) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//   if (req == NULL) {
//     pr_err("req is NULL");
//   }
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   APPEND_EVENT_IF_VALID(
//       list_last_entry(&rec->ts->list, struct ts_entry, list)->event !=
//       NVME_TCP_TRY_SEND_DATA,
//       rec, now, NVME_TCP_TRY_SEND);
//   UNLOCKQ(qid);
// }

// void on_nvme_tcp_try_send_cmd_pdu(void* ignore, struct request* req,
//                                   struct socket* sock, int qid, int len) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   APPEND_EVENT_IF_VALID(true, rec, now, NVME_TCP_TRY_SEND_CMD_PDU);
//   UNLOCKQ(qid);
// }

// void on_nvme_tcp_try_send_data_pdu(void* ignore, struct request* req, void* pdu,
//                                    int qid) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   if (rec) {
//     append_event(rec, now, NVME_TCP_TRY_SEND_DATA_PDU);
//     ((struct nvme_tcp_data_pdu*)pdu)->stat.tag = true;
//   }
//   UNLOCKQ(qid);
// }

// void on_nvme_tcp_try_send_data(void* ignore, struct request* req, void* pdu,
//                                int qid) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   APPEND_EVENT_IF_VALID(
//       list_last_entry(&rec->ts->list, struct ts_entry, list)->event !=
//       NVME_TCP_TRY_SEND,
//       rec, now, NVME_TCP_TRY_SEND_DATA);
//   UNLOCKQ(qid);
// }

void on_nvme_tcp_done_send_req(void* ignore, struct request* req, int qid) {
  CHECK_TRACE_CONDITIONS(qid);
  u64 now = ktime_get_real_ns();
  
  LOCKQ(qid);
  struct profile_record* rec = get_profile_record(&stat[qid], req);
  APPEND_EVENT_IF_VALID(true, rec, now, NVME_TCP_DONE_SEND_REQ);
  UNLOCKQ(qid);
}

void on_nvme_tcp_handle_c2h_data(void* ignore, struct request* rq, int qid,
                                 int data_remain, u64 recv_time) {
  CHECK_TRACE_CONDITIONS(qid);
  int cid = smp_processor_id();
  u64 now = ktime_get_real_ns();
  LOCKQ(qid);
  struct profile_record* rec = get_profile_record(&stat[qid], rq);
  if (rec) {
    rec->metadata.contains_c2h = 1;
    append_event(rec, recv_time, NVME_TCP_RECV_SKB);
    append_event(rec, now, NVME_TCP_HANDLE_C2H_DATA);
  }
  UNLOCKQ(qid);
}

// void on_nvme_tcp_recv_data(void* ignore, struct request* req, int qid, int len) {
//   CHECK_TRACE_CONDITIONS(qid);
//   u64 now = ktime_get_real_ns();
//
//   LOCKQ(qid);
//   struct profile_record* rec = get_profile_record(&stat[qid], req);
//   APPEND_EVENT_IF_VALID(true, rec, now, NVME_TCP_RECV_DATA);
//   UNLOCKQ(qid);
// }

void cpy_ntprof_stat_to_record(struct profile_record* record,
                               struct ntprof_stat* pdu_stat) {
  if (unlikely(pdu_stat == NULL)) {
    pr_err("try to copy time series from pdu_stat but it is NULL");
    return;
  }
  int i;
  for (i = 0; i < pdu_stat->cnt; i++) {
    append_event(record, pdu_stat->ts[i], pdu_stat->event[i]);
  }
}


void on_nvme_tcp_handle_r2t(void* ignore, struct request* rq, void* pdu,
                            int qid, u64 recv_time) {
  CHECK_TRACE_CONDITIONS(qid);
  u64 now = ktime_get_real_ns();
  
  LOCKQ(qid);
  struct profile_record* rec = get_profile_record(&stat[qid], rq);
  if (rec) {
    if (((struct nvme_tcp_r2t_pdu*)pdu)->stat.id != (unsigned long long)rec->
        metadata.cmdid) {
      pr_warn("stat.id=%llu, metadata.cmdid=%d\n",
              ((struct nvme_tcp_r2t_pdu *) pdu)->stat.id,
              rec->metadata.cmdid);
    } else {
      rec->metadata.contains_r2t = 1;
      // cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_r2t_pdu*)pdu)->stat);
    }

    append_event(rec, recv_time, NVME_TCP_RECV_SKB);
    append_event(rec, now, NVME_TCP_HANDLE_R2T);
  } else {
    pr_err(
        "!!! on_nvme_tcp_handle_r2t: rec [tag=%d] is not found or it is NULL, stat->cmdid=%llu, the incomplete queue is: ",
        rq->tag, ((struct nvme_tcp_r2t_pdu *) pdu)->stat.id);
  }
  UNLOCKQ(qid);
}

void on_nvme_tcp_process_nvme_cqe(void* ignore, struct request* rq, void* pdu,
                                  int qid, u64 recv_time) {
  CHECK_TRACE_CONDITIONS(qid);
  u64 now = ktime_get_real_ns();
  
  LOCKQ(qid);
  struct profile_record* rec = get_profile_record(&stat[qid], rq);
  if (rec) {
    if (((struct nvme_tcp_rsp_pdu*)pdu)->stat.id != (unsigned long long)rec->
        metadata.cmdid) {
      // pr_warn("stat.id=%llu, metadata.cmdid=%d\n", ((struct nvme_tcp_rsp_pdu *) pdu)->stat.id,
      // rec->metadata.cmdid);
    } else {
      // cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_rsp_pdu*)pdu)->stat);
      append_event(rec, recv_time, NVME_TCP_RECV_SKB);
      append_event(rec, now, NVME_TCP_PROCESS_NVME_CQE);
    }
  }
  UNLOCKQ(qid);
}



// register tracepoints
static struct nvmf_tracepoint tracepoints[] = {
    TRACEPOINT_ENTRY(nvme_tcp_queue_rq),
    // TRACEPOINT_ENTRY(nvme_tcp_queue_request),
    // TRACEPOINT_ENTRY(nvme_tcp_try_send),
    // TRACEPOINT_ENTRY(nvme_tcp_try_send_cmd_pdu),
    // TRACEPOINT_ENTRY(nvme_tcp_try_send_data_pdu),
    // TRACEPOINT_ENTRY(nvme_tcp_try_send_data),
    TRACEPOINT_ENTRY(nvme_tcp_done_send_req),
    TRACEPOINT_ENTRY(nvme_tcp_handle_c2h_data),
    // TRACEPOINT_ENTRY(nvme_tcp_recv_data),
    TRACEPOINT_ENTRY(nvme_tcp_handle_r2t),
    TRACEPOINT_ENTRY(nvme_tcp_process_nvme_cqe),
};

int register_nvme_tcp_tracepoints(void) {
  int ret;
  int registered = 0;

  for (; registered < ARRAY_SIZE(tracepoints); registered++) {
    struct nvmf_tracepoint* t = &tracepoints[registered];

    ret = t->register_fn(t->handler, NULL);
    if (ret) {
      pr_err("Failed to register tracepoint %d (handler: %ps)\n",
             registered, t->handler);
      goto unregister;
    }
  }
  return 0;

unregister:
  while (--registered >= 0) {
    tracepoints[registered].
        unregister_fn(tracepoints[registered].handler, NULL);
  }
  return ret;
}

void unregister_nvme_tcp_tracepoints(void) {
  int i;

  for (i = ARRAY_SIZE(tracepoints) - 1; i >= 0; i--) {
    tracepoints[i].unregister_fn(tracepoints[i].handler, NULL);
  }
}