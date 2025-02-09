#include "target.h"
#include <linux/list.h>
#include "target_logging.h"

void init_per_queue_statistics(struct per_queue_statistics *pqs) {
    INIT_LIST_HEAD(&pqs->records);
    spin_lock_init(&pqs->lock);
}

void free_per_queue_statistics(struct per_queue_statistics *pqs) {
    if (pqs == NULL) {
        pr_err("Try to free a per_queue_statistics but it is NULL\n");
        return;
    }
    unsigned long flags;
    spin_lock_irqsave(&pqs->lock, flags);
    struct list_head *pos, *q;
    struct profile_record *record;
    list_for_each_safe(pos, q, &pqs->records) {
        record = list_entry(pos, struct profile_record, list);
        list_del_init(pos);
        free_profile_record(record);
    }
    spin_unlock_irqrestore(&pqs->lock, flags);
}

void append_record(struct per_queue_statistics *pqs, struct profile_record *r) {
    unsigned long flags;
    spin_lock_irqsave(&pqs->lock, flags);
    list_add_tail(&r->list, &pqs->records);
    spin_unlock_irqrestore(&pqs->lock, flags);
}

int try_remove_record(struct per_queue_statistics *pqs, int cmdid, void *op, unsigned long long timestamp,
                       enum EEvent event, struct ntprof_stat *s) {
    fn f = (fn) op;

    unsigned long flags;
    spin_lock_irqsave(&pqs->lock, flags);
    struct list_head *pos;
    struct profile_record *record;
    list_for_each(pos, &pqs->records) {
        record = list_entry(pos, struct profile_record, list);
        if (record->metadata.req_tag == cmdid) {
            // append the last event to the record
            // copy the profile record to resp pdu
            f(record, timestamp, event, s);
            // remove the record from the list
            list_del_init(&record->list);
            free_profile_record(record);
            spin_unlock_irqrestore(&pqs->lock, flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&pqs->lock, flags);
    return -1;
}


struct profile_record *get_profile_record(struct per_queue_statistics *pqs, int cmdid) {
    unsigned long flags;
    spin_lock_irqsave(&pqs->lock, flags);
    struct list_head *pos;
    struct profile_record *record;
    list_for_each(pos, &pqs->records) {
        record = list_entry(pos, struct profile_record, list);
        if (record->metadata.req_tag == cmdid) {
            spin_unlock_irqrestore(&pqs->lock, flags);
            return record;
        }
    }
    spin_unlock_irqrestore(&pqs->lock, flags);
    return NULL;
}
