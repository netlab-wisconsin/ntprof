#include "host.h"

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include "host_logging.h"

void init_per_core_statistics(struct per_core_statistics *stats) {
    stats->sampler = 0;
    INIT_LIST_HEAD(&stats->incomplete_records);
    INIT_LIST_HEAD(&stats->completed_records);
}

void free_per_core_statistics(struct per_core_statistics *stats) {
    if (stats == NULL) {
        pr_err("Try to free a per_core_statistics but it is NULL\n");
        return;
    }
    // use free_profile_record to free all records
    struct list_head *pos, *q;
    struct profile_record *record;
    list_for_each_safe(pos, q, &stats->incomplete_records) {
        record = list_entry(pos, struct profile_record, list);
        list_del_init(pos);
        free_profile_record(record);
    }

    list_for_each_safe(pos, q, &stats->completed_records) {
        record = list_entry(pos, struct profile_record, list);
        list_del_init(pos);
        free_profile_record(record);
    }
}

void append_record(struct per_core_statistics *stats, struct profile_record *record) {
    // pr_info("append a record to the incomplete list, %d\n", record->metadata.req_tag);
    list_add_tail(&record->list, &stats->incomplete_records);
}

void complete_record(struct per_core_statistics *stats, struct profile_record *record) {
    list_del(&record->list);
    list_add_tail(&record->list, &stats->completed_records);
}

/**
 * get the profile record with the given request tag from the incomplete list
 */
struct profile_record *get_profile_record(struct per_core_statistics *stats, int req_tag) {
    struct list_head *pos;
    struct profile_record *record;
    list_for_each(pos, &stats->incomplete_records) {
        record = list_entry(pos, struct profile_record, list);
        if (record->metadata.req_tag == req_tag) {
            return record;
        }
    }
    return NULL;
}

/**
 * convert a string to an integer
 * If the string is NULL or does not start with a digit, return -1
 * If the string starts with a digit, convert the string to an integer in the beginning, 20A24 -> 20
 */
int strtoint(const char *str, int *num) {
    if (unlikely(str == NULL || !isdigit(str[0]))) {
        pr_err("[ntprof_host] try to convert str to int, but str is NULL\n");
        *num = -1;
        return -EINVAL;
    }
    *num = 0;
    int i = 0;
    while (isdigit(str[i])) {
        *num = *num * 10 + (str[i] - '0');
        i++;
    }
    return 0;
}


/**
 * Parse NVMe device name, considering the following format:
 *  - nvmeXnY, X is the controller id, Y is the namespace id
 *  - nvmeXcYnZ, X is the controller id, Y is the sub-controller id, Z is the namespace id
 *  If it is of the second format, we ignore the sub-controller id.
 * NOTE: assume the input name is in the correct format.
 */
int parse_nvme_name(const char *name, int *ctrl_id, int *ns_id) {
    // check if the name starts with "nvme"
    if (strncmp(name, "nvme", 4) != 0) {
        return 0;
    }

    // skip "nvme"
    const char *p = name + 4;
    int x, z;

    // 1. parse the controller ID
    if (!isdigit((unsigned char) *p))
        return 0; // "nvme" must be followed by a digit

    if (strtoint(p, &x) < 0) {
        return 0;
    }

    // update controller ID
    *ctrl_id = (int) x;

    // 2. parse the namespace ID
    const char *last_n = strrchr(p, 'n');
    if (!last_n || !isdigit((unsigned char) last_n[1])) {
        return 0;
    }

    if (strtoint(last_n + 1, &z) < 0) {
        return 0;
    }

    // update namespace ID
    *ns_id = (int) z;
    return 1;
}

/**
 * Compare two NVMe device names to check if they refer to the same device.
 */
bool is_same_dev_name(const char *name1, const char *name2) {
    if (name1 == name2 || strcmp(name1, name2) == 0)
        return true;

    int c1, n1, c2, n2;
    parse_nvme_name(name1, &c1, &n1);
    parse_nvme_name(name2, &c2, &n2);
    // pr_info("name1=%s, name2=%s, c1=%d, n1=%d, c2=%d, n2=%d\n", name1, name2, c1, n1, c2, n2);
    return parse_nvme_name(name1, &c1, &n1) &&
           parse_nvme_name(name2, &c2, &n2) &&
           (c1 == c2) && (n1 == n2);
}


bool match_config(struct request *req, struct ntprof_config *config) {
    // check type
    if (config->io_type != BOTH && config->io_type != rq_data_dir(req)) {
        return false;
    }

    // check size
    unsigned int io_size = blk_rq_bytes(req);
    if (config->min_io_size == -1 && config->max_io_size == -1) {
        if (config->io_size_num > 0) {
            int i;
            bool is_found = false;
            for (i = 0; i < config->io_size_num; i++) {
                if (config->io_size[i] == io_size) {
                    is_found = 1;
                    break;
                }
            }
            if (!is_found) return false;
        }
    } else if (config->min_io_size != -1 && io_size < config->min_io_size) {
        return false;
    } else if (config->max_io_size != -1 && io_size > config->max_io_size) {
        return false;
    }

    // todo: check core id

    // check device name
    // if config->session_name is "all", return true
    if (strcmp(config->session_name, "all")) {
        if (!req->rq_disk || !req->rq_disk->disk_name ||
            !is_same_dev_name(req->rq_disk->disk_name, config->session_name)) {
            return false;
        }
    }
    return true;
}
