#ifndef STATISTICS_H
#define STATISTICS_H

// this header should only be included in the kernel module

#include "config.h"
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/nvme-tcp.h>

/* Event type definitions (X-macro) */
#define EVENT_LIST \
  X(BLK_SUBMIT) \
  X(BLK_RQ_COMPLETE) \
  X(NVME_TCP_QUEUE_RQ) \
  X(NVME_TCP_QUEUE_REQUEST) \
  X(NVME_TCP_TRY_SEND) \
  X(NVME_TCP_TRY_SEND_CMD_PDU) \
  X(NVME_TCP_TRY_SEND_DATA_PDU) \
  X(NVME_TCP_TRY_SEND_DATA) \
  X(NVME_TCP_DONE_SEND_REQ) \
  X(NVME_TCP_RECV_SKB) \
  X(NVME_TCP_HANDLE_C2H_DATA) \
  X(NVME_TCP_RECV_DATA) \
  X(NVME_TCP_HANDLE_R2T) \
  X(NVME_TCP_PROCESS_NVME_CQE) \
  X(NVMET_TCP_TRY_RECV_PDU) \
  X(NVMET_TCP_DONE_RECV_PDU) \
  X(NVMET_TCP_EXEC_READ_REQ) \
  X(NVMET_TCP_EXEC_WRITE_REQ) \
  X(NVMET_TCP_QUEUE_RESPONSE) \
  X(NVMET_TCP_SETUP_C2H_DATA_PDU) \
  X(NVMET_TCP_SETUP_R2T_PDU) \
  X(NVMET_TCP_SETUP_RESPONSE_PDU) \
  X(NVMET_TCP_TRY_SEND_DATA_PDU) \
  X(NVMET_TCP_TRY_SEND_R2T) \
  X(NVMET_TCP_TRY_SEND_RESPONSE) \
  X(NVMET_TCP_TRY_SEND_DATA) \
  X(NVMET_TCP_HANDLE_H2C_DATA_PDU) \
  X(NVMET_TCP_TRY_RECV_DATA) \
  X(NVMET_TCP_IO_WORK)

enum EEvent {
#define X(a) a,
  EVENT_LIST
#undef X
};

static inline char* event_to_string(enum EEvent event) {
  switch (event) {
#define X(a) case a: return #a;
    EVENT_LIST
#undef X
    default:
      return "UNKNOWN";
  }
}

struct profile_record {
  struct {
    struct request* req;
    char disk[MAX_SESSION_NAME_LEN];
    int req_tag;
    int cmdid;
    int size; // in bytes
    int is_write; // 0 for read, 1 for write
    int contains_c2h; // 0 for true, 1 for false
    int contains_r2t; // 0 for true, 1 for false
  } metadata;

  // struct ts_entry* ts;

  // *** READ (initiator side)
  // [0] blk_submit -> blk layer | 1s
  // [1] nvme_tcp_queue_rq -> nvme-tcp layer | 1s
  // [2] nvme_tcp_done_send_req -> wait
  // [3] nvme_tcp_recv_skb -> nstack | 1c
  // [4] nvme_tcp_handle_c2h_data -> nvme-tcp layer | 1c
  // [5] nvme_tcp_process_nvme_cqe -> blk layer | 1c
  // [6] nvme_tcp_blk_req_complete

  // *** SMALL SIZE WRITE (initiator side)
  // [0] blk_submit -> blk layer | 1s
  // [1] nvme_tcp_queue_rq -> nvme-tcp layer | 1s
  // [2] nvme_tcp_done_send_req -> waiting
  // [3] nvme_tcp_recv_skb -> nstack | 1c
  // [4] nvme_tcp_process_nvme_cqe -> blk layer | 1c
  // [5] blk_req_complete

  // *** BIG SIZE WRITE (initiator side)
  // [0] blk_submit -> blk layer | 1s
  // [1] queue_rq -> nvme-tcp layer | 1s
  // [2] done_send_req -> waiting | 1
  // [3] recv_skb
  // [4] handle_r2t -> nstack | 1c
  // [5] done_send_req -> nvme-tcp | 1s
  // [6] recv_skb -> loading | 2
  // [7] process_nvme_cqe -> nstack | 1c
  // [8] blk_req_complete -> blk layer | 1c
  u64 timestamps[MAX_EVENT_NUM];
  enum EEvent events[MAX_EVENT_NUM];
  size_t cnt;

  struct list_head list;

  struct ntprof_stat stat1;
  struct ntprof_stat stat2;
};

/** ---------- predefined sequence template ---------- **/
typedef struct {
  const enum EEvent* events;
  const enum EEvent* target1;
  const enum EEvent* target2;
  size_t length;
  size_t target1_length;
  size_t target2_length;
  const char* description;
} EventSequence;

static const EventSequence seq_read = {
    .events = (enum EEvent[]){
        BLK_SUBMIT,
        NVME_TCP_QUEUE_RQ,
        NVME_TCP_DONE_SEND_REQ,
        NVME_TCP_RECV_SKB,
        NVME_TCP_HANDLE_C2H_DATA,
        NVME_TCP_PROCESS_NVME_CQE,
        BLK_RQ_COMPLETE
    },
    .target1 = (enum EEvent[]){
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_DONE_RECV_PDU,
        NVMET_TCP_EXEC_READ_REQ,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_TRY_SEND_RESPONSE,
    },
    .target2 = (enum EEvent[]){},
    .length = 7,
    .target1_length = 5,
    .target2_length = 0,
    .description = "Read Sequence"
};

static const EventSequence seq_small_write = {
    .events = (enum EEvent[]){
        BLK_SUBMIT,
        NVME_TCP_QUEUE_RQ,
        NVME_TCP_DONE_SEND_REQ,
        NVME_TCP_RECV_SKB,
        NVME_TCP_PROCESS_NVME_CQE,
        BLK_RQ_COMPLETE
    },
    .target1 = (enum EEvent[]){
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_DONE_RECV_PDU,
        NVMET_TCP_EXEC_WRITE_REQ,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_TRY_SEND_RESPONSE,
    },
    .target2 = (enum EEvent[]){},
    .length = 6,
    .target1_length = 5,
    .target2_length = 0,
    .description = "Small Write Sequence"
};

static const EventSequence seq_big_write = {
    .events = (enum EEvent[]){
        BLK_SUBMIT,
        NVME_TCP_QUEUE_RQ,
        NVME_TCP_DONE_SEND_REQ,
        NVME_TCP_RECV_SKB,
        NVME_TCP_HANDLE_R2T,
        NVME_TCP_DONE_SEND_REQ,
        NVME_TCP_RECV_SKB,
        NVME_TCP_PROCESS_NVME_CQE,
        BLK_RQ_COMPLETE
    },
    .target1 = (enum EEvent[]){
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_DONE_RECV_PDU,
        NVMET_TCP_TRY_SEND_R2T,
    },
    .target2 = (enum EEvent[]){
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_HANDLE_H2C_DATA_PDU,
        NVMET_TCP_EXEC_WRITE_REQ,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_TRY_SEND_RESPONSE,
    },
    .length = 9,
    .target1_length = 3,
    .target2_length = 5,
    .description = "Big Write Sequence"
};

static int validate_sequence(struct profile_record* r, const EventSequence* seq,
                             const char* warn_msg,
                             int (*extra_check)(struct profile_record*)) {
  if (r->cnt != seq->length) {
    pr_warn("%s: Expected %zu events, got %lu (%s)\n",
            warn_msg, seq->length, r->cnt, seq->description);
    return 0;
  }

  if (extra_check && !extra_check(r)) {
    pr_warn("%s: Extra check failed\n", warn_msg);
    return 0;
  }

  int i;
  for (i = 0; i < seq->length; i++) {
    if (r->events[i] != seq->events[i]) {
      pr_warn("%s: Expected %s at %d, got %s\n", warn_msg,
              event_to_string(seq->events[i]), i,
              event_to_string(r->events[i]));
      return 0;
    }
    if (r->timestamps[i] == 0) {
      pr_warn("%s: timestamp=0 at %d\n", warn_msg, i);
      return 0;
    }
  }

#ifdef MAX_EVENT_NUM
  for (i = seq->length; i < MAX_EVENT_NUM; i++) {
    if (r->events[i] != -1 || r->timestamps[i] != 0) {
      pr_warn("%s: Dirty data at index %d\n", warn_msg, i);
      return 0;
    }
  }
#endif

  // check target
  if (seq->target1_length > 0) {
    if (r->stat1.cnt != seq->target1_length) {
      pr_warn("%s: Expected %zu target1 events, got %u\n",
              warn_msg, seq->target1_length, r->stat1.cnt);
      return 0;
    }
    for (i = 0; i < seq->target1_length; i++) {
      if (r->stat1.event[i] != seq->target1[i]) {
        pr_warn("%s: Expected %s at target1 %d, got %s\n", warn_msg,
                event_to_string(seq->target1[i]), i,
                event_to_string(r->stat1.event[i]));
        return 0;
      }
      if (r->stat1.ts[i] == 0) {
        pr_warn("%s: target1 timestamp=0 at %d\n", warn_msg, i);
        return 0;
      }
    }
  }

  if (seq->target2_length > 0) {
    if (r->stat2.cnt != seq->target2_length) {
      pr_warn("%s: Expected %zu target2 events, got %u\n",
              warn_msg, seq->target2_length, r->stat2.cnt);
      return 0;
    }
    for (i = 0; i < seq->target2_length; i++) {
      if (r->stat2.event[i] != seq->target2[i]) {
        pr_warn("%s: Expected %s at target2 %d, got %s\n", warn_msg,
                event_to_string(seq->target2[i]), i,
                event_to_string(r->stat2.event[i]));
        return 0;
      }
      if (r->stat2.ts[i] == 0) {
        pr_warn("%s: target2 timestamp=0 at %d\n", warn_msg, i);
        return 0;
      }
    }
  }

  return 1;
}

static inline int is_valid_read(struct profile_record* r) {
  return validate_sequence(r, &seq_read, "INVALID_READ_RECORD", NULL);
}

static inline int is_valid_write(struct profile_record* r) {
  const EventSequence* seq = r->metadata.contains_r2t
                               ? &seq_big_write
                               : &seq_small_write;
  return validate_sequence(r, seq, "INVALID_WRITE_RECORD", NULL);
}

static inline int is_valid_profile_record(struct profile_record* r) {
  return r->metadata.is_write ? is_valid_write(r) : is_valid_read(r);
}

static inline void print_profile_record(struct profile_record* record) {
  pr_info(
      "Record: disk=%s, tag=%d, size=%d, is_write=%d, contains_c2h=%d, contains_r2t=%d, cnt=%d\n",
      record->metadata.disk, record->metadata.req_tag, record->metadata.size,
      record->metadata.is_write,
      record->metadata.contains_c2h, record->metadata.contains_r2t, record->cnt);
  int i;
  for (i = 0; i < record->cnt; i++) {
    pr_info("  [%d] %s: %llu\n", i, event_to_string(record->events[i]),
            record->timestamps[i]);
  }

  pr_info("  [FROM TARGET]");
  for (i = 0; i < record->stat1.cnt; i++) {
    pr_info("  [%d] %s: %llu\n", i, event_to_string(record->stat1.event[i]),
            record->stat1.ts[i]);
  }
  if (record->metadata.contains_r2t) {
    for (i = 0; i < record->stat2.cnt; i++) {
      pr_info("  [%d] %s: %llu\n", i, event_to_string(record->stat2.event[i]),
              record->stat2.ts[i]);
    }
  }
}


static inline void init_profile_record(struct profile_record* record, int size,
                                       int is_write, char* disk, int tag) {
  record->metadata.size = size;
  record->metadata.is_write = is_write;
  record->metadata.req_tag = tag;
  strncpy(record->metadata.disk, disk, MAX_SESSION_NAME_LEN - 1);
  record->metadata.disk[MAX_SESSION_NAME_LEN - 1] = '\0';

  record->metadata.contains_c2h = 0;
  record->metadata.contains_r2t = 0;
  record->metadata.cmdid = -1;

  record->cnt = 0;
  int i = 0;
  for (i = 0; i < MAX_EVENT_NUM; i++) {
    record->timestamps[i] = 0;
    record->events[i] = -1;
  }
  INIT_LIST_HEAD(&record->list);
}

/**
 * append a single <time, event> pair at the end of the timeseries list
 */
static inline void append_event(struct profile_record* record,
                                unsigned long long timestamp,
                                enum EEvent event) {
  if (record->cnt == MAX_EVENT_NUM) {
    pr_warn("append_event: record is full, cannot append more events\n");
    return;
  }
  record->timestamps[record->cnt] = timestamp;
  record->events[record->cnt] = event;
  record->cnt++;
}

static inline enum EEvent get_last_event(struct profile_record* record) {
  if (record->cnt == 0) {
    pr_warn("get_last_event: record is empty\n");
    return -1;
  }
  return record->events[record->cnt - 1];
}

static inline void free_profile_record(struct profile_record* record) {
  if (unlikely(!record)) return;
  if (!list_empty(&record->list)) {
    pr_warn("free_profile_record: Attempting to free record still linked!\n");
    return;
  }
  kfree(record);
}

#endif //STATISTICS_H