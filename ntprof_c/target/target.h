#ifndef TARGET_H
#define TARGET_H

#define MAX_QUEUE_NUM 64

#include <linux/spinlock.h>
#include "../include/statistics.h"

struct per_queue_statistics {
    struct list_head records;
    struct spinlock lock;
};

void init_per_queue_statistics(struct per_queue_statistics *pqs);

void free_per_queue_statistics(struct per_queue_statistics *pqs);

void append_record(struct per_queue_statistics *pqs, struct profile_record *r);

struct profile_record *get_profile_record(struct per_queue_statistics *pqs, int cmdid);

extern struct per_queue_statistics stat[MAX_QUEUE_NUM];



#endif //TARGET_H
