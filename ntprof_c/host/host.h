#ifndef HOST_H
#define HOST_H

#include "../include/config.h"
#include "../include/statistics.h"
#include <linux/types.h>
#include <linux/blkdev.h>

struct per_core_statistics {
    unsigned long long sampler;
    struct list_head incomplete_records;
    struct list_head completed_records;
};

void init_per_core_statistics(struct per_core_statistics *stats) ;

void append_record(struct per_core_statistics *stats, struct profile_record *record);

void complete_record(struct per_core_statistics *stats, struct profile_record *record);

struct profile_record* get_profile_record(struct per_core_statistics *stats, int req_tag);

bool match_config(struct request *req, struct ntprof_config *config);

extern struct per_core_statistics stat[128];

extern struct ntprof_config global_config;

#endif //HOST_H
