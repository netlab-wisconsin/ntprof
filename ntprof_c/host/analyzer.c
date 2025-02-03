// analyzer.c
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "analyzer.h"

#define MAX_CATEGORIES 32

struct categorized_records {
    struct list_head records;
    int io_size;
    int io_type;
    char session_name[MAX_SESSION_NAME_LEN];
};

static struct categorized_records categories[MAX_CATEGORIES];
static int category_count = 0;
static DEFINE_MUTEX(category_mutex);

static int categorize_record(struct profile_record *record, struct ntprof_config *config) {
    int i;

    mutex_lock(&category_mutex);

    for (i = 0; i < category_count; ++i) {
        bool match = true;

        if (config->enable_group_by_size && categories[i].io_size != record->metadata.size) {
            match = false;
        }
        if (config->enable_group_by_type && categories[i].io_type != record->metadata.is_write) {
            match = false;
        }
        if (config->enable_group_by_session && strcmp(categories[i].session_name, config->session_name) != 0) {
            match = false;
        }

        if (match) {
            list_move_tail(&record->list, &categories[i].records);
            mutex_unlock(&category_mutex);
            return 0;
        }
    }

    if (category_count < MAX_CATEGORIES) {
        INIT_LIST_HEAD(&categories[category_count].records);
        categories[category_count].io_size = record->metadata.size;
        categories[category_count].io_type = record->metadata.is_write;
        strncpy(categories[category_count].session_name, config->session_name, MAX_SESSION_NAME_LEN - 1);
        categories[category_count].session_name[MAX_SESSION_NAME_LEN - 1] = '\0';

        list_move_tail(&record->list, &categories[category_count].records);
        category_count++;
    } else {
        pr_warn("categorize_record: Reached MAX_CATEGORIES limit\n");
        mutex_unlock(&category_mutex);
        return -ENOMEM;
    }

    mutex_unlock(&category_mutex);
    return 0;
}

static int analyze_per_core_profile_records(void *data) {
    struct per_core_statistics *core_stat = (struct per_core_statistics *) data;
    struct profile_record *record, *tmp;

    list_for_each_entry_safe(record, tmp, &core_stat->completed_records, list) {
        if (kthread_should_stop()) break;
        categorize_record(record, &global_config);
    }

    // clear completed_records list and incompleted_recordslist

    core_stat->is_cleared = 1;

    return 0;
}

void analyze_statistics(struct per_core_statistics *stat, struct ntprof_config *config) {
    int i;
    struct task_struct *threads[MAX_CORE_NUM];

    // update global config, report part
    global_config.enable_group_by_session = config->enable_group_by_session;
    global_config.enable_group_by_size = config->enable_group_by_size;
    global_config.enable_group_by_type = config->enable_group_by_type;
    global_config.enable_latency_distribution = config->enable_latency_distribution;
    global_config.enable_throughput = config->enable_throughput;
    global_config.enable_latency_breakdown = config->enable_latency_breakdown;
    global_config.enable_queue_length = config->enable_queue_length;

    for (i = 0; i < MAX_CORE_NUM; ++i) {
        if (!list_empty(&stat[i].completed_records)) {
            threads[i] = kthread_run(analyze_per_core_profile_records, &stat[i], "analyze_per_core_profile_records_%d",
                                     i);
        } else {
            threads[i] = NULL;
        }
    }

    for (i = 0; i < MAX_CORE_NUM; ++i) {
        if (threads[i]) {
            int ret = kthread_stop(threads[i]);
            if (ret != 0) {
                pr_warn("Thread analyze_per_core_profile_records_%d did not exit cleanly\n", i);
            }
        }
    }


}
