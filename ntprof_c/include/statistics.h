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
    NVME_TCP_RECV_SKB,
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
        case NVME_TCP_RECV_SKB:
            return "NVME_TCP_RECV_SKB";
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
        int cmdid;
        int size; // in bytes
        int is_write; // 0 for read, 1 for write
        int contains_c2h; // 0 for true, 1 for false
        int contains_r2t; // 0 for true, 1 for false
    } metadata;

    struct ts_entry *ts;

    struct list_head list;
};

// [21815.693780]     timestamp=1739105422908690767, event=BLK_SUBMIT
// [21815.693782]     timestamp=1739105422908692600, event=NVME_TCP_QUEUE_RQ
// [21815.693782]     timestamp=1739105422908696241, event=NVME_TCP_QUEUE_REQUEST
// [21815.693783]     timestamp=1739105422908697104, event=NVME_TCP_TRY_SEND
// [21815.693784]     timestamp=1739105422908712754, event=NVME_TCP_TRY_SEND_CMD_PDU
// [21815.693785]     timestamp=1739105422908713235, event=NVME_TCP_DONE_SEND_REQ
// [21815.693785]     timestamp=1739105422909338715, event=NVME_TCP_RECV_SKB
// [21815.693786]     timestamp=1739105422909359041, event=NVME_TCP_HANDLE_C2H_DATA
// [21815.693787]     timestamp=1739105422909365130, event=NVME_TCP_RECV_DATA
// [21815.693787]     timestamp=1739105422908821122, event=NVMET_TCP_TRY_RECV_PDU
// [21815.693788]     timestamp=1739105422908841019, event=NVMET_TCP_DONE_RECV_PDU
// [21815.693789]     timestamp=1739105422908973388, event=NVMET_TCP_EXEC_READ_REQ
// [21815.693789]     timestamp=1739105422909338452, event=NVMET_TCP_QUEUE_RESPONSE
// [21815.693790]     timestamp=1739105422909341494, event=NVMET_TCP_SETUP_C2H_DATA_PDU
// [21815.693791]     timestamp=1739105422909341717, event=NVMET_TCP_TRY_SEND_DATA_PDU
// [21815.693791]     timestamp=1739105422909344089, event=NVMET_TCP_TRY_SEND_DATA
// [21815.693792]     timestamp=1739105422909404902, event=NVMET_TCP_SETUP_RESPONSE_PDU
// [21815.693793]     timestamp=1739105422909405073, event=NVMET_TCP_TRY_SEND_RESPONSE
// [21815.693793]     timestamp=1739105422909629212, event=NVME_TCP_RECV_SKB
// [21815.693794]     timestamp=1739105422909709478, event=NVME_TCP_PROCESS_NVME_CQE
// [21815.693795]     timestamp=1739105422909712048, event=BLK_RQ_COMPLETE

static inline int is_valid_read(struct profile_record *r) {
    struct ts_entry *entry = NULL;  // 确保 entry 是 NULL 初始化
    enum EEvent expected_event_sequence[] = {
        BLK_SUBMIT,                           //0
        NVME_TCP_QUEUE_RQ,                    //1
        NVME_TCP_QUEUE_REQUEST,              //2
        NVME_TCP_TRY_SEND,                   //3
        NVME_TCP_TRY_SEND_CMD_PDU, //4
        NVME_TCP_DONE_SEND_REQ,///5
        NVME_TCP_RECV_SKB,//6
        NVME_TCP_HANDLE_C2H_DATA,//
        NVME_TCP_RECV_DATA,//8
        NVMET_TCP_TRY_RECV_PDU,//9
        NVMET_TCP_DONE_RECV_PDU,//10
        NVMET_TCP_EXEC_READ_REQ,//11
        NVMET_TCP_QUEUE_RESPONSE,//12
        NVMET_TCP_SETUP_C2H_DATA_PDU,//13
        NVMET_TCP_TRY_SEND_DATA_PDU,//14
        NVMET_TCP_TRY_SEND_DATA,//15
        NVMET_TCP_SETUP_RESPONSE_PDU,//16
        NVMET_TCP_TRY_SEND_RESPONSE,//17
        NVME_TCP_RECV_SKB,//18
        NVME_TCP_PROCESS_NVME_CQE,//19
        BLK_RQ_COMPLETE//20
    };

    int expected_index = 0;
    unsigned long long hosttime = 0;
    unsigned long long targettime = 0;

    // 确保 r->ts 是有效的链表头
    list_for_each_entry(entry, &r->ts->list, list) {
        // 如果事件超过了预期的数量，说明有多余事件
        if (expected_index >= sizeof(expected_event_sequence) / sizeof(expected_event_sequence[0])) {
            pr_err("More events than expected in idx [%d]! Extra event found: %s", expected_index,
                   event_to_string(entry->event));
            return 0; // 多余的事件
        }

        // 检查事件类型是否符合预期
        if (entry->event != expected_event_sequence[expected_index]) {
            pr_err("Event order is not expected in idx [%d]! Expected: %s, Found: %s", expected_index,
                   event_to_string(expected_event_sequence[expected_index]),
                   event_to_string(entry->event));
            return 0; // 事件顺序不匹配
        }

        // 检查时间戳递增
        if (entry->event >= NVMET_TCP_TRY_RECV_PDU && entry->event <= NVMET_TCP_IO_WORK) {
            // 对于 NVMET 事件，时间戳应该递增
            if (entry->timestamp < targettime) {
                pr_err("Timestamp on target is not increasing on idx[%d]! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       targettime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            targettime = entry->timestamp;
        } else {
            // 对于 BLK 事件，时间戳应该递增
            if (entry->timestamp < hosttime) {
                pr_err("Timestamp on host is not increasing on idx[%d! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       hosttime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            hosttime = entry->timestamp;
        }

        expected_index++;

    }
    return 1; // 所有事件都符合预期
}

// [21764.846386]     timestamp=1739105372062063411, event=BLK_SUBMIT
// [21764.846387]     timestamp=1739105372062064681, event=NVME_TCP_QUEUE_RQ
// [21764.846388]     timestamp=1739105372062066394, event=NVME_TCP_QUEUE_REQUEST
// [21764.846389]     timestamp=1739105372062066779, event=NVME_TCP_TRY_SEND
// [21764.846390]     timestamp=1739105372062070726, event=NVME_TCP_TRY_SEND_CMD_PDU
// [21764.846390]     timestamp=1739105372062077535, event=NVME_TCP_TRY_SEND_DATA
// [21764.846391]     timestamp=1739105372062077715, event=NVME_TCP_DONE_SEND_REQ
// [21764.846392]     timestamp=1739105372062200225, event=NVMET_TCP_TRY_RECV_PDU
// [21764.846393]     timestamp=1739105372062220703, event=NVMET_TCP_DONE_RECV_PDU
// [21764.846393]     timestamp=1739105372062224744, event=NVMET_TCP_TRY_RECV_DATA
// [21764.846394]     timestamp=1739105372062225056, event=NVMET_TCP_EXEC_WRITE_REQ
// [21764.846395]     timestamp=1739105372062244244, event=NVMET_TCP_QUEUE_RESPONSE
// [21764.846395]     timestamp=1739105372062247083, event=NVMET_TCP_SETUP_RESPONSE_PDU
// [21764.846396]     timestamp=1739105372062247257, event=NVMET_TCP_TRY_SEND_RESPONSE
// [21764.846397]     timestamp=1739105372062284215, event=NVME_TCP_RECV_SKB
// [21764.846398]     timestamp=1739105372062302508, event=NVME_TCP_PROCESS_NVME_CQE
// [21764.846398]     timestamp=1739105372062304609, event=BLK_RQ_COMPLETE

static inline int is_valid_writes(struct profile_record *r) {
    struct ts_entry *entry = r->ts;
    struct ts_entry *prev_entry = NULL;

    // 定义事件顺序和类型
    enum EEvent expected_event_sequence[] = {
        BLK_SUBMIT,
        NVME_TCP_QUEUE_RQ,
        NVME_TCP_QUEUE_REQUEST,
        NVME_TCP_TRY_SEND,
        NVME_TCP_TRY_SEND_CMD_PDU,
        NVME_TCP_TRY_SEND_DATA,
        NVME_TCP_DONE_SEND_REQ,
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_DONE_RECV_PDU,
        NVMET_TCP_TRY_RECV_DATA,
        NVMET_TCP_EXEC_WRITE_REQ,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_SETUP_RESPONSE_PDU,
        NVMET_TCP_TRY_SEND_RESPONSE,
        NVME_TCP_RECV_SKB,
        NVME_TCP_PROCESS_NVME_CQE,
        BLK_RQ_COMPLETE
    };

    int expected_index = 0;
    unsigned long long hosttime = 0;
    unsigned long long targettime = 0;

    // 确保 r->ts 是有效的链表头
    list_for_each_entry(entry, &r->ts->list, list) {
        // 如果事件超过了预期的数量，说明有多余事件
        if (expected_index >= sizeof(expected_event_sequence) / sizeof(expected_event_sequence[0])) {
            pr_err("More events than expected in idx [%d]! Extra event found: %s", expected_index,
                   event_to_string(entry->event));
            return 0; // 多余的事件
        }

        // 检查事件类型是否符合预期
        if (entry->event != expected_event_sequence[expected_index]) {
            pr_err("Event order is not expected in idx [%d]! Expected: %s, Found: %s", expected_index,
                   event_to_string(expected_event_sequence[expected_index]),
                   event_to_string(entry->event));
            return 0; // 事件顺序不匹配
        }

        // 检查时间戳递增
        if (entry->event >= NVMET_TCP_TRY_RECV_PDU && entry->event <= NVMET_TCP_IO_WORK) {
            // 对于 NVMET 事件，时间戳应该递增
            if (entry->timestamp < targettime) {
                pr_err("Timestamp on target is not increasing on idx[%d]! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       targettime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            targettime = entry->timestamp;
        } else {
            // 对于 BLK 事件，时间戳应该递增
            if (entry->timestamp < hosttime) {
                pr_err("Timestamp on host is not increasing on idx[%d! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       hosttime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            hosttime = entry->timestamp;
        }

        expected_index++;

    }

    return 1; // 所有事件都符合预期
}

// [21639.922280]     timestamp=1739105247137164976, event=BLK_SUBMIT
// [21639.922282]     timestamp=1739105247137166069, event=NVME_TCP_QUEUE_RQ
// [21639.922283]     timestamp=1739105247137168514, event=NVME_TCP_QUEUE_REQUEST
// [21639.922283]     timestamp=1739105247137169057, event=NVME_TCP_TRY_SEND
// [21639.922284]     timestamp=1739105247137178866, event=NVME_TCP_TRY_SEND_CMD_PDU
// [21639.922285]     timestamp=1739105247137179113, event=NVME_TCP_DONE_SEND_REQ
// [21639.922285]     timestamp=1739105247137272830, event=NVMET_TCP_TRY_RECV_PDU
// [21639.922286]     timestamp=1739105247137293624, event=NVMET_TCP_DONE_RECV_PDU
// [21639.922287]     timestamp=1739105247137427639, event=NVMET_TCP_QUEUE_RESPONSE
// [21639.922288]     timestamp=1739105247137428808, event=NVMET_TCP_SETUP_R2T_PDU
// [21639.922288]     timestamp=1739105247137429151, event=NVMET_TCP_TRY_SEND_R2T
// [21639.922289]     timestamp=1739105247137423752, event=NVME_TCP_RECV_SKB
// [21639.922290]     timestamp=1739105247137438229, event=NVME_TCP_HANDLE_R2T
// [21639.922290]     timestamp=1739105247137439064, event=NVME_TCP_QUEUE_REQUEST
// [21639.922291]     timestamp=1739105247137441013, event=NVME_TCP_TRY_SEND
// [21639.922292]     timestamp=1739105247137441171, event=NVME_TCP_TRY_SEND_DATA_PDU
// [21639.922292]     timestamp=1739105247137443512, event=NVME_TCP_TRY_SEND_DATA
// [21639.922293]     timestamp=1739105247137520990, event=NVME_TCP_DONE_SEND_REQ
// [21639.922294]     timestamp=1739105247137489909, event=NVMET_TCP_TRY_RECV_PDU
// [21639.922294]     timestamp=1739105247137501269, event=NVMET_TCP_HANDLE_H2C_DATA_PDU
// [21639.922295]     timestamp=1739105247137516413, event=NVMET_TCP_TRY_RECV_DATA
// [21639.922296]     timestamp=1739105247137839508, event=NVMET_TCP_EXEC_WRITE_REQ
// [21639.922296]     timestamp=1739105247138168673, event=NVMET_TCP_QUEUE_RESPONSE
// [21639.922297]     timestamp=1739105247138172102, event=NVMET_TCP_SETUP_RESPONSE_PDU
// [21639.922298]     timestamp=1739105247138172312, event=NVMET_TCP_TRY_SEND_RESPONSE
// [21639.922298]     timestamp=1739105247138156723, event=NVME_TCP_RECV_SKB
// [21639.922299]     timestamp=1739105247138160630, event=NVME_TCP_PROCESS_NVME_CQE
// [21639.922300]     timestamp=1739105247138161931, event=BLK_RQ_COMPLETE
static inline int is_valid_writel(struct profile_record *r) {
    struct ts_entry *entry = r->ts;
    struct ts_entry *prev_entry = NULL;

    // 定义事件顺序和类型
    enum EEvent expected_event_sequence[] = {
        BLK_SUBMIT,
        NVME_TCP_QUEUE_RQ,
        NVME_TCP_QUEUE_REQUEST,
        NVME_TCP_TRY_SEND,
        NVME_TCP_TRY_SEND_CMD_PDU,
        NVME_TCP_DONE_SEND_REQ,
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_DONE_RECV_PDU,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_SETUP_R2T_PDU,
        NVMET_TCP_TRY_SEND_R2T,
        NVME_TCP_RECV_SKB,
        NVME_TCP_HANDLE_R2T,
        NVME_TCP_QUEUE_REQUEST,
        NVME_TCP_TRY_SEND,
        NVME_TCP_TRY_SEND_DATA_PDU,
        NVME_TCP_TRY_SEND_DATA,
        NVME_TCP_DONE_SEND_REQ,
        NVMET_TCP_TRY_RECV_PDU,
        NVMET_TCP_HANDLE_H2C_DATA_PDU,
        NVMET_TCP_TRY_RECV_DATA,
        NVMET_TCP_EXEC_WRITE_REQ,
        NVMET_TCP_QUEUE_RESPONSE,
        NVMET_TCP_SETUP_RESPONSE_PDU,
        NVMET_TCP_TRY_SEND_RESPONSE,
        NVME_TCP_RECV_SKB,
        NVME_TCP_PROCESS_NVME_CQE,
        BLK_RQ_COMPLETE
    };

    int expected_index = 0;
    unsigned long long hosttime = 0;
    unsigned long long targettime = 0;

    // 确保 r->ts 是有效的链表头
    list_for_each_entry(entry, &r->ts->list, list) {
        // 如果事件超过了预期的数量，说明有多余事件
        if (expected_index >= sizeof(expected_event_sequence) / sizeof(expected_event_sequence[0])) {
            pr_err("More events than expected in idx [%d]! Extra event found: %s", expected_index,
                   event_to_string(entry->event));
            return 0; // 多余的事件
        }

        // 检查事件类型是否符合预期
        if (entry->event != expected_event_sequence[expected_index]) {
            pr_err("Event order is not expected in idx [%d]! Expected: %s, Found: %s", expected_index,
                   event_to_string(expected_event_sequence[expected_index]),
                   event_to_string(entry->event));
            return 0; // 事件顺序不匹配
        }

        // 检查时间戳递增
        if (entry->event >= NVMET_TCP_TRY_RECV_PDU && entry->event <= NVMET_TCP_IO_WORK) {
            // 对于 NVMET 事件，时间戳应该递增
            if (entry->timestamp < targettime) {
                pr_err("Timestamp on target is not increasing on idx[%d]! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       targettime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            targettime = entry->timestamp;
        } else {
            // 对于 BLK 事件，时间戳应该递增
            if (entry->timestamp < hosttime) {
                pr_err("Timestamp on host is not increasing on idx[%d! Previous timestamp: %llu, Current timestamp: %llu", expected_index,
                       hosttime, entry->timestamp);
                return 0; // 时间戳递减，不符合预期
            }
            hosttime = entry->timestamp;
        }

        expected_index++;

    }

    return 1; // 所有事件都符合预期
}

static inline int is_valid_profile_record(struct profile_record *r) {
    if (!r->metadata.is_write) {
        return is_valid_read(r);
    } else if (!r->metadata.contains_r2t) {
        return is_valid_writes(r);
    } else {
        return is_valid_writel(r);
    }
}

static inline void print_profile_record(struct profile_record *record) {
    struct ts_entry *entry;
    pr_info("Record: disk=%s, tag=%d, size=%d, is_write=%d, contains_c2h=%d, contains_r2t=%d\n",
            record->metadata.disk, record->metadata.req_tag, record->metadata.size, record->metadata.is_write,
            record->metadata.contains_c2h, record->metadata.contains_r2t);
    int cnt = 0;
    list_for_each_entry(entry, &record->ts->list, list) {
        pr_info("    [%d]timestamp=%llu, event=%s\n", cnt++, entry->timestamp, event_to_string(entry->event));
    }
}


static inline void init_profile_record(struct profile_record *record, int size, int is_write, char *disk, int tag) {
    record->metadata.size = size;
    record->metadata.is_write = is_write;
    record->metadata.req_tag = tag;
    strncpy(record->metadata.disk, disk, MAX_SESSION_NAME_LEN);

    record->metadata.contains_c2h = 0;
    record->metadata.contains_r2t = 0;
    record->metadata.cmdid = -1;

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

    // deduplicate, if the last event is the same as the current event, we don't add it
    if (!list_empty(&record->ts->list) && list_last_entry(&record->ts->list, struct ts_entry, list)->event == event) {
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
