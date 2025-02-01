#include "target.h"
#include <linux/list.h>
#include "target_logging.h"

void init_per_queue_statistics(struct per_queue_statistics *pqs) {
    INIT_LIST_HEAD(&pqs->records);
}

void free_per_queue_statistics(struct per_queue_statistics *pqs) {
    if (pqs == NULL) {
        pr_err("Try to free a per_queue_statistics but it is NULL\n");
        return;
    }
    struct list_head *pos, *q;
    struct profile_record *record;
    list_for_each_safe(pos, q, &pqs->records) {
        record = list_entry(pos, struct profile_record, list);
        list_del_init(pos);
        free_profile_record(record);
    }
}

void append_record(struct per_queue_statistics *pqs, struct profile_record *r) {
    list_add_tail(&r->list, &pqs->records);
}

struct profile_record *get_profile_record(struct per_queue_statistics *pqs, int cmdid) {
    struct list_head *pos;
    struct profile_record *record;
    list_for_each(pos, &pqs->records) {
        record = list_entry(pos, struct profile_record, list);
        if (record->metadata.cmdid == cmdid) {
            return record;
        }
    }
    return NULL;
}
