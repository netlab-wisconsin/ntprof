#ifndef STATISTICS_H
#define STATISTICS_H

// this header should only be included in the kernel module

#include "config.h"
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/slab.h>

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

// return event name in string given enum
static inline char *event_to_string(enum EEvent event) {
    switch (event) {
        case BLK_SUBMIT:
            return "BLK_SUBMIT";
        case BLK_RQ_COMPLETE:
            return "BLK_RQ_COMPLETE";
        case NVME_TCP_QUEUE_RQ:
            return "NVME_TCP_QUEUE_RQ";
        case NVME_TCP_QUEUE_REQUEST:
            return "NVME_TCP_QUEUE_REQUEST";
        case NVME_TCP_TRY_SEND:
            return "NVME_TCP_TRY_SEND";
        case NVME_TCP_TRY_SEND_CMD_PDU:
            return "NVME_TCP_TRY_SEND_CMD_PDU";
        case NVME_TCP_TRY_SEND_DATA_PDU:
            return "NVME_TCP_TRY_SEND_DATA_PDU";
        case NVME_TCP_TRY_SEND_DATA:
            return "NVME_TCP_TRY_SEND_DATA";
        case NVME_TCP_DONE_SEND_REQ:
            return "NVME_TCP_DONE_SEND_REQ";
        case NVME_TCP_TRY_RECV:
            return "NVME_TCP_TRY_RECV";
        case NVME_TCP_HANDLE_C2H_DATA:
            return "NVME_TCP_HANDLE_C2H_DATA";
        case NVME_TCP_RECV_DATA:
            return "NVME_TCP_RECV_DATA";
        case NVME_TCP_HANDLE_R2T:
            return "NVME_TCP_HANDLE_R2T";
        case NVME_TCP_PROCESS_NVME_CQE:
            return "NVME_TCP_PROCESS_NVME_CQE";
        case NVMET_TCP_TRY_RECV_PDU:
            return "NVMET_TCP_TRY_RECV_PDU";
        case NVMET_TCP_DONE_RECV_PDU:
            return "NVMET_TCP_DONE_RECV_PDU";
        case NVMET_TCP_EXEC_READ_REQ:
            return "NVMET_TCP_EXEC_READ_REQ";
        case NVMET_TCP_EXEC_WRITE_REQ:
            return "NVMET_TCP_EXEC_WRITE_REQ";
        case NVMET_TCP_QUEUE_RESPONSE:
            return "NVMET_TCP_QUEUE_RESPONSE";
        case NVMET_TCP_SETUP_C2H_DATA_PDU:
            return "NVMET_TCP_SETUP_C2H_DATA_PDU";
        case NVMET_TCP_SETUP_R2T_PDU:
            return "NVMET_TCP_SETUP_R2T_PDU";
        case NVMET_TCP_SETUP_RESPONSE_PDU:
            return "NVMET_TCP_SETUP_RESPONSE_PDU";
        case NVMET_TCP_TRY_SEND_DATA_PDU:
            return "NVMET_TCP_TRY_SEND_DATA_PDU";
        case NVMET_TCP_TRY_SEND_R2T:
            return "NVMET_TCP_TRY_SEND_R2T";
        case NVMET_TCP_TRY_SEND_RESPONSE:
            return "NVMET_TCP_TRY_SEND_RESPONSE";
        case NVMET_TCP_TRY_SEND_DATA:
            return "NVMET_TCP_TRY_SEND_DATA";
        case NVMET_TCP_HANDLE_H2C_DATA_PDU:
            return "NVMET_TCP_HANDLE_H2C_DATA_PDU";
        case NVMET_TCP_TRY_RECV_DATA:
            return "NVMET_TCP_TRY_RECV_DATA";
        case NVMET_TCP_IO_WORK:
            return "NVMET_TCP_IO_WORK";
        default:
            return "UNKNOWN";
    }
}


struct ts_entry {
    unsigned long long timestamp;
    enum EEvent event;
    struct list_head list;
};

struct profile_record {
    struct {
        char disk[MAX_SESSION_NAME_LEN];
        int req_tag;
        int size; // in bytes
        int is_write; // 0 for read, 1 for write
        int contains_c2h; // 0 for true, 1 for false
        int contains_r2t; // 0 for true, 1 for false
    } metadata;

    struct ts_entry *ts;

    struct list_head list;
};

static inline void print_profile_record(struct profile_record *record) {
    struct ts_entry *entry;
    pr_info("Record: disk=%s, tag=%d, size=%d, is_write=%d, contains_c2h=%d, contains_r2t=%d\n",
            record->metadata.disk, record->metadata.req_tag, record->metadata.size, record->metadata.is_write,
            record->metadata.contains_c2h, record->metadata.contains_r2t);
    list_for_each_entry(entry, &record->ts->list, list) {
        pr_info("    timestamp=%llu, event=%s\n", entry->timestamp, event_to_string(entry->event));
    }
}


static inline void init_profile_record(struct profile_record *record, int size, int is_write, char *disk, int tag) {
    record->metadata.size = size;
    record->metadata.is_write = is_write;
    record->metadata.req_tag = tag;
    strncpy(record->metadata.disk, disk, MAX_SESSION_NAME_LEN);

    record->metadata.contains_c2h = 0;
    record->metadata.contains_r2t = 0;

    record->ts = kmalloc(sizeof(struct ts_entry), GFP_KERNEL);
    if (unlikely(!record->ts)) {
        pr_err("Failed to allocate memory for ts_entry list head\n");
        return;
    }
    INIT_LIST_HEAD(&record->ts->list);
    INIT_LIST_HEAD(&record->list);
}

/**
 * append a single <time, event> pair at the end of the timeseries list
 */
static inline void append_event(struct profile_record *record, unsigned long long timestamp, enum EEvent event) {
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
static inline void append_events(struct profile_record *record, struct ts_entry *to_append) {
    if (unlikely(!record->ts)) {
        pr_err("append_events: record->ts is NULL, did you forget to call init_profile_record?\n");
        return;
    }

    if (unlikely(!to_append)) {
        pr_warn("append_events: to_append is NULL, nothing to append\n");
        return;
    }

    list_splice_tail(&to_append->list, &record->ts->list);
    kfree(to_append);
}

static inline void free_timeseries(struct ts_entry *timeseries) {
    struct ts_entry *entry, *tmp;
    if (unlikely(!timeseries)) return;
    list_for_each_entry_safe(entry, tmp, &timeseries->list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    kfree(timeseries);
}

static inline void free_profile_record(struct profile_record *record) {
    if (unlikely(!record)) return;
    if (!list_empty(&record->list)) {
        pr_warn("free_profile_record: Attempting to free record still linked!\n");
        return;
    }
    free_timeseries(record->ts);
    kfree(record);
}

#endif //STATISTICS_H
