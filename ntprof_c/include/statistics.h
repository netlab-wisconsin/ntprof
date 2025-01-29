#ifndef STATISTICS_H
#define STATISTICS_H

// this header should only be included in the kernel module

#include "config.h"
#include <linux/list.h>
#include <linux/compiler.h>

enum EEvent {
    // ---------------------
    // BLK Events
    // ---------------------
    BLK_SUBMIT,
    BLK_RQ_COMPLETE,

    // ----------------------
    // NVMe over TCP Events
    // ----------------------
    NVME_TCP_QUEUE_RQ,
    NVME_TCP_QUEUE_REQUEST,
    NVME_TCP_TRY_SEND,
    NVME_TCP_TRY_SEND_CMD_PDU,
    NVME_TCP_TRY_SEND_DATA_PDU,
    NVME_TCP_TRY_SEND_DATA,
    NVME_TCP_DONE_SEND_REQ,
    NVME_TCP_TRY_RECV,
    NVME_TCP_HANDLE_C2H_DATA,
    NVME_TCP_RECV_DATA,
    NVME_TCP_HANDLE_R2T,
    NVME_TCP_PROCESS_NVME_CQE,

    // ----------------------
    // NVMe over TCP Events
    // ----------------------
    NVMET_TCP_TRY_RECV_PDU,
    NVMET_TCP_DONE_RECV_PDU,
    NVMET_TCP_EXEC_READ_REQ,
    NVMET_TCP_EXEC_WRITE_REQ,
    NVMET_TCP_QUEUE_RESPONSE,
    NVMET_TCP_SETUP_C2H_DATA_PDU,
    NVMET_TCP_SETUP_R2T_PDU,
    NVMET_TCP_SETUP_RESPONSE_PDU,
    NVMET_TCP_TRY_SEND_DATA_PDU,
    NVMET_TCP_TRY_SEND_R2T,
    NVMET_TCP_TRY_SEND_RESPONSE,
    NVMET_TCP_TRY_SEND_DATA,
    NVMET_TCP_HANDLE_H2C_DATA_PDU,
    NVMET_TCP_TRY_RECV_DATA,
    NVMET_TCP_IO_WORK,
};

struct metadata {
    int size; // in bytes
    int is_write; // 0 for read, 1 for write
    int contains_c2h; // 0 for true, 1 for false
    int contains_r2t; // 0 for true, 1 for false
};


struct ts_entry {
    unsigned long long timestamp;
    enum EEvent event;
    struct list_head list;
};

struct profile_record {
    struct metadata meta;
    struct ts_entry *ts;
};


inline void init_profile_record(struct profile_record *record, int size, int is_write) {
    record->meta.size = size;
    record->meta.is_write = is_write;
    record->meta.contains_c2h = 0;
    record->meta.contains_r2t = 0;

    // 先分配 `record->ts`，避免空指针访问
    record->ts = kmalloc(sizeof(struct ts_entry), GFP_KERNEL);
    if (unlikely(!record->ts)) {
        pr_err("Failed to allocate memory for ts_entry list head\n");
        return;
    }
    INIT_LIST_HEAD(&record->ts->list);
}

/**
 * append a single <time, event> pair at the end of the timeseries list
 */
inline void append_event(struct profile_record *record, unsigned long long timestamp, enum EEvent event) {
    struct ts_entry *new_entry;

    if (unlikely(!record->ts)) {
        pr_err("append_event: record->ts is NULL, did you forget to call init_profile_record?\n");
        return;
    }

    new_entry = kmalloc(sizeof(struct ts_entry), GFP_KERNEL);
    if (unlikely(!new_entry)) {
        pr_err("Failed to allocate memory for ts_entry\n");
        return;
    }

    new_entry->timestamp = timestamp;
    new_entry->event = event;
    INIT_LIST_HEAD(&new_entry->list);
    list_add_tail(&new_entry->list, &record->ts->list);
}

/**
 * Append a list of <time, event> pairs at the end of the timeseries list in record
 *
 * NOTE: DO NOT FREE THE MEMORY OF to_append AFTER CALLING THIS FUNCTION
 * SINCE WE USE THE OLD POINTERS DIRECTLY
 */
inline void append_events(struct profile_record *record, struct ts_entry *to_append) {
    if (unlikely(!record->ts)) {
        pr_err("append_events: record->ts is NULL, did you forget to call init_profile_record?\n");
        return;
    }

    if (unlikely(!to_append)) {
        pr_warn("append_events: to_append is NULL, nothing to append\n");
        return;
    }

    list_splice_tail(&to_append->list, &record->ts->list);
}


inline void free_timeseries(struct ts_entry *timeseries) {
    struct ts_entry *entry, *tmp;
    if (unlikely(!timeseries)) return;
    list_for_each_entry_safe(entry, tmp, &timeseries->list, list) {
        list_del(&entry->list);
        kfree(entry);
    }

    kfree(timeseries);
}

#endif //STATISTICS_H
