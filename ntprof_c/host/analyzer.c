#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jhash.h>
#include <linux/bitops.h>

#include "../include/analyze.h"
#include "analyzer.h"
#include "statistics.h"
#include "breakdown.h"

#define CATEGORY_HASH_BITS (ilog2(MAX_CATEGORIES - 1) + 1)

// hash table to store categorized profiling records
static DEFINE_HASHTABLE(categories_hash, CATEGORY_HASH_BITS);
static atomic_t categories_count = ATOMIC_INIT(0);

// multiple threads will try to find and update the (key -> categorized_records) hashmap
static DEFINE_MUTEX(category_mutex);

// *************************************************
//  Phase 1 - categorize the profiling records
// *************************************************

// we maintain a hashtable, (category_key --> categorized_records)

struct category_key {
    int io_size;
    int io_type;
    char session_name[MAX_SESSION_NAME_LEN];
};

// a collection of profile records, of the same type (identified by the key)
struct categorized_records {
    struct hlist_node hash_node;
    struct list_head records;
    struct category_key key;
    // multiple threads will insert new elements to a categorized_records
    atomic_t count;
};

// given a profiling record and the configuration, insert it to the hashtable
static int categorize_record(struct profile_record *record, struct ntprof_config *config) {
    // if core id is 16
    int cid = smp_processor_id();
    if (cid == 16) {
        pr_info("categorize_record is called!");
        print_profile_record(record);
        if (!record->metadata.is_write) {
            struct read_breakdown rb;
            init_read_breakdown(&rb);
            break_latency_read(record, &rb);
            print_read_breakdown(&rb);
        } else if (record->metadata.contains_r2t) {
            struct write_breakdown_l wb;
            init_write_breakdown_l(&wb);
            break_latency_write_l(record, &wb);
            print_write_breakdown_l(&wb);
        } else {
            struct write_breakdown_s wb;
            init_write_breakdown_s(&wb);
            break_latency_write_s(record, &wb);
            print_write_breakdown_s(&wb);
        }
    }
    // generate the category key
    struct category_key key = {
        .io_size = config->enable_group_by_size ? record->metadata.size : -1,
        .io_type = config->enable_group_by_type ? record->metadata.is_write : -1,
    };
    const char *session = config->enable_group_by_session ? config->session_name : "";
    strncpy(key.session_name, session, MAX_SESSION_NAME_LEN - 1);
    key.session_name[MAX_SESSION_NAME_LEN - 1] = '\0';

    // check if the key exists, if not, try to create a new one
    u32 hash = jhash(&key, sizeof(key), 0);
    struct categorized_records *cat;
    bool found = false;

    mutex_lock(&category_mutex);
    hash_for_each_possible(categories_hash, cat, hash_node, hash) {
        if (memcmp(&cat->key, &key, sizeof(key)) == 0) {
            found = true;
            break;
        }
    }

    if (found) {
        // we find the corresponding entry in the hashmap
        // insert the profiling record to the bucket
        list_move_tail(&record->list, &cat->records);
        atomic_inc(&cat->count);
    } else {
        // generate a new entry (category) in the hashmap and insert the new profiling record
        if (atomic_read(&categories_count) < MAX_CATEGORIES) {
            cat = kzalloc(sizeof(*cat), GFP_KERNEL);
            if (!cat) {
                mutex_unlock(&category_mutex);
                return -ENOMEM;
            }

            memcpy(&cat->key, &key, sizeof(key));
            INIT_LIST_HEAD(&cat->records);
            atomic_set(&cat->count, 1);
            list_move_tail(&record->list, &cat->records);

            hash_add(categories_hash, &cat->hash_node, hash);
            atomic_inc(&categories_count);
        } else {
            mutex_unlock(&category_mutex);
            pr_warn_ratelimited("Reached max categories limit\n");
            return -ENOSPC;
        }
    }

    mutex_unlock(&category_mutex);
    return 0;
}

// given a collection of profiling records (collected by the same core)
// move and group them based on the categories
static void categorize_records_of_each_core(struct per_core_statistics *stat) {
    struct profile_record *rec, *tmp;
    list_for_each_entry_safe(rec, tmp, &stat->completed_records, list) {
        categorize_record(rec, &global_config);
    }
    // remove tall elements in the incompleted_records
    list_for_each_entry_safe(rec, tmp, &stat->incomplete_records, list) {
        list_del(&rec->list);
        kfree(rec);
    }
}

struct analysis_work {
    struct work_struct work;
    struct per_core_statistics *stat;
};

static void categorize_work_fn(struct work_struct *work) {
    pr_info("categorize_work_fn start working");
    struct analysis_work *w = container_of(work, struct analysis_work, work);
    categorize_records_of_each_core(w->stat);
    kfree(w);
}

void start_phase_1(struct per_core_statistics *stat, struct ntprof_config *config) {
    int i;
    struct workqueue_struct *wq;

    // Create workqueue outside of the mutex scope
    wq = alloc_workqueue("categorize_wq", WQ_UNBOUND, 0);
    if (!wq) {
        pr_warn("Failed to create workqueue\n");
        return;
    }

    mutex_lock(&category_mutex);
    memcpy(&global_config, config, sizeof(global_config));
    mutex_unlock(&category_mutex);

    for (i = 0; i < MAX_CORE_NUM; ++i) {
        if (list_empty(&stat[i].completed_records))
            continue;

        struct analysis_work *work = kmalloc(sizeof(*work), GFP_KERNEL);
        if (!work) {
            pr_warn("Failed to allocate work for core %d\n", i);
            continue;
        }

        INIT_WORK(&work->work, categorize_work_fn);
        work->stat = &stat[i];
        queue_work(wq, &work->work);
    }

    flush_workqueue(wq);
    destroy_workqueue(wq);
}

// *******************************************************
//  Phase 2 - summarize each category and generate report
// *******************************************************

void summarize_category(struct categorized_records *cat, struct report *rpt, int idx)
{
    struct profile_record *record, *tmp;
    list_for_each_entry_safe(record, tmp, &cat->records, list) {
        list_del(&record->list);  // Remove from list
        rpt->total_io++;                // Update the report count
        if (!record->metadata.is_write) {
            break_latency_read(record, &rpt->breakdown->read);
        } else if (!record->metadata.contains_r2t) {
           break_latency_write_s(record, &rpt->breakdown->writes);
        } else {
            break_latency_write_l(record, &rpt->breakdown->writel);
        }
        kfree(record);
    }
}

struct summarize_work {
    struct work_struct work;
    struct categorized_records *cat;
    struct report local_result;
    struct report *global_result;
    int id;
};

// this whole function is protected by the category_mutex
static void merge_result(struct report *global, struct report *local) {
    global->total_io += local->total_io;
}

static void summarize_work_fn(struct work_struct *work)
{
    pr_info("summarize_work_fn start working!");
    struct summarize_work *sw = container_of(work, struct summarize_work, work);
    struct categorized_records *cat = sw->cat;

    summarize_category(cat, &sw->local_result, sw->id);

    pr_info("Category => size=%d, type=%d, session=%s, total_cnt=%llu\n",
            cat->key.io_size,
            cat->key.io_type,
            cat->key.session_name,
            sw->local_result.total_io);

    mutex_lock(&category_mutex);
    // udpate the global report
    merge_result(sw->global_result, &(sw->local_result));
    hash_del(&cat->hash_node);
    kfree(cat);  // Free category after summarization
    mutex_unlock(&category_mutex);

    kfree(sw);
}

static void start_phase_2(struct report *result) {
    struct workqueue_struct *wq = alloc_workqueue("summary_wq", WQ_UNBOUND, 0);

    if (!wq) {
        pr_warn("Failed to create summary workqueue\n");
        return;
    }

    mutex_lock(&category_mutex);

    int bkt;
    struct hlist_node *tmp;
    struct categorized_records *cat;

    int cnt = 0;
    hash_for_each_safe(categories_hash, bkt, tmp, cat, hash_node) {
        struct summarize_work *sw = kmalloc(sizeof(*sw), GFP_KERNEL);
        if (!sw) {
            pr_warn("Failed to allocate summarize_work\n");
            continue;
        }

        INIT_WORK(&sw->work, summarize_work_fn);
        sw->cat = cat;
        sw->global_result = result;
        sw->id = cnt++;
        queue_work(wq, &sw->work);
    }

    result->cnt=cnt;

    mutex_unlock(&category_mutex);

    flush_workqueue(wq);
    destroy_workqueue(wq);
}

// Initialization and cleanup functions
static void preparing_analyzation(void) {
    hash_init(categories_hash);
    atomic_set(&categories_count, 0);
}

static void finish_analyzation(void) {
    int bkt;
    struct hlist_node *tmp;
    struct categorized_records *cat;

    mutex_lock(&category_mutex);
    hash_for_each_safe(categories_hash, bkt, tmp, cat, hash_node) {
        hash_del(&cat->hash_node);
        kfree(cat);
    }
    mutex_unlock(&category_mutex);
}

void analyze(struct ntprof_config *conf, struct report *rpt) {
    pr_info("start preparing analysis phase!");
    preparing_analyzation();

    pr_info("start phase 1: categorize the profile records");
    start_phase_1(stat, conf);

    pr_info("start phase 2: summarize the profile records in each category");
    start_phase_2(rpt);

    pr_info("finish analysis, clean up the hashmap");
    finish_analyzation();
}

// // analyzer.c
// #include <linux/slab.h>
// #include <linux/list.h>
// #include <linux/mutex.h>
// #include <linux/kthread.h>
// #include <linux/sched.h>
// #include <linux/delay.h>
//
// #include "analyzer.h"
//
// #define MAX_CATEGORIES 32
//
// struct categorized_records {
//     struct list_head records;
//     int io_size;
//     int io_type;
//     char session_name[MAX_SESSION_NAME_LEN];
// };
//
// static struct categorized_records categories[MAX_CATEGORIES];
// static int category_count = 0;
// static DEFINE_MUTEX(category_mutex);
//
// static int categorize_record(struct profile_record *record, struct ntprof_config *config) {
//     int i;
//
//     mutex_lock(&category_mutex);
//
//     for (i = 0; i < category_count; ++i) {
//         bool match = true;
//
//         if (config->enable_group_by_size && categories[i].io_size != record->metadata.size) {
//             match = false;
//         }
//         if (config->enable_group_by_type && categories[i].io_type != record->metadata.is_write) {
//             match = false;
//         }
//         if (config->enable_group_by_session && strcmp(categories[i].session_name, config->session_name) != 0) {
//             match = false;
//         }
//
//         if (match) {
//             list_move_tail(&record->list, &categories[i].records);
//             mutex_unlock(&category_mutex);
//             return 0;
//         }
//     }
//
//     if (category_count < MAX_CATEGORIES) {
//         INIT_LIST_HEAD(&categories[category_count].records);
//         categories[category_count].io_size = record->metadata.size;
//         categories[category_count].io_type = record->metadata.is_write;
//         strncpy(categories[category_count].session_name, config->session_name, MAX_SESSION_NAME_LEN - 1);
//         categories[category_count].session_name[MAX_SESSION_NAME_LEN - 1] = '\0';
//
//         list_move_tail(&record->list, &categories[category_count].records);
//         category_count++;
//     } else {
//         pr_warn("categorize_record: Reached MAX_CATEGORIES limit\n");
//         mutex_unlock(&category_mutex);
//         return -ENOMEM;
//     }
//
//     mutex_unlock(&category_mutex);
//     return 0;
// }
//
// static int analyze_per_core_profile_records(void *data) {
//     struct per_core_statistics *core_stat = (struct per_core_statistics *) data;
//     struct profile_record *record, *tmp;
//
//     list_for_each_entry_safe(record, tmp, &core_stat->completed_records, list) {
//         if (kthread_should_stop()) break;
//         categorize_record(record, &global_config);
//     }
//
//     // clear incomplete_records list
//     list_for_each_entry_safe(record, tmp, &core_stat->incomplete_records, list) {
//         list_del(&record->list);
//         kfree(record);
//     }
//
//     // if complete_records list is not empty, print a warning and clean it
//     if (!list_empty(&core_stat->completed_records)) {
//         pr_warn("analyze_per_core_profile_records: non-empty completed_records list\n");
//         list_for_each_entry_safe(record, tmp, &core_stat->completed_records, list) {
//             list_del(&record->list);
//             kfree(record);
//         }
//     }
//     core_stat->is_cleared = 1;
//     return 0;
// }
//
// /*
// *BLK_SUBMIT
// NVME_TCP_QUEUE_RQ
// NVME_TCP_QUEUE_REQUEST
// NVME_TCP_TRY_SEND
// NVME_TCP_TRY_SEND_CMD_PDU
// NVME_TCP_DONE_SEND_REQ
// NVME_TCP_RECV_SKB
// NVME_TCP_HANDLE_C2H_DATA
// NVME_TCP_RECV_DATA
// NVMET_TCP_TRY_RECV_PDU
// NVMET_TCP_DONE_RECV_PDU
// NVMET_TCP_EXEC_READ_REQ
// NVMET_TCP_QUEUE_RESPONSE
// NVMET_TCP_SETUP_C2H_DATA_PDU
// NVMET_TCP_TRY_SEND_DATA_PDU
// NVMET_TCP_TRY_SEND_DATA
// NVMET_TCP_SETUP_RESPONSE_PDU
// NVMET_TCP_TRY_SEND_RESPONSE
// NVME_TCP_RECV_SKB
// NVME_TCP_PROCESS_NVME_CQE
// BLK_RQ_COMPLETE
// */
// void break_latency_read(struct profile_record *record, struct read_breakdown *breakdown) {
//     struct ts_entry *entry = record->ts;
//     unsigned long long blk_submit = 0,
//             blk_complete = 0,
//             nvme_tcp_queue_rq = 0,
//             nvme_tcp_queue_request = 0,
//             nvme_tcp_try_send = 0,
//             nvme_tcp_try_send_cmd_pdu = 0,
//             nvme_tcp_done_send_req = 0,
//             nvme_tcp_recv_skb1 = 0,
//             nvme_tcp_recv_skb2 = 0,
//             nvme_tcp_handle_c2h_data = 0,
//             nvme_tcp_recv_data = 0,
//             nvme_tcp_process_nvme_cqe = 0,
//             nvmet_tcp_try_recv_pdu = 0,
//             nvmet_tcp_done_recv_pdu = 0,
//             nvmet_tcp_exec_read_req = 0,
//             nvmet_tcp_queue_response = 0,
//             nvmet_tcp_setup_c2h_data_pdu = 0,
//             nvmet_tcp_try_send_data_pdu = 0,
//             nvmet_tcp_try_send_data = 0,
//             nvmet_tcp_setup_response_pdu = 0,
//             nvmet_tcp_try_send_response = 0;
//
//     // Traverse the timestamp list and map events to variables
//     int nvme_tcp_recv_skb_cnt = 0;
//     list_for_each_entry(entry, &record->ts->list, list) {
//         switch (entry->event) {
//             case BLK_SUBMIT: blk_submit = entry->timestamp;
//                 break;
//             case BLK_RQ_COMPLETE: blk_complete = entry->timestamp;
//                 break;
//             case NVME_TCP_QUEUE_RQ: nvme_tcp_queue_rq = entry->timestamp;
//                 break;
//             case NVME_TCP_QUEUE_REQUEST: nvme_tcp_queue_request = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND: nvme_tcp_try_send = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND_CMD_PDU: nvme_tcp_try_send_cmd_pdu = entry->timestamp;
//                 break;
//             case NVME_TCP_DONE_SEND_REQ: nvme_tcp_done_send_req = entry->timestamp;
//                 break;
//             case NVME_TCP_RECV_SKB:
//                 if (nvme_tcp_recv_skb_cnt == 0) {
//                     nvme_tcp_recv_skb1 = entry->timestamp;
//                 } else {
//                     nvme_tcp_recv_skb2 = entry->timestamp;
//                 }
//                 nvme_tcp_recv_skb_cnt++;
//                 break;
//             case NVME_TCP_HANDLE_C2H_DATA: nvme_tcp_handle_c2h_data = entry->timestamp;
//                 break;
//             case NVME_TCP_RECV_DATA: nvme_tcp_recv_data = entry->timestamp;
//                 break;
//             case NVME_TCP_PROCESS_NVME_CQE: nvme_tcp_process_nvme_cqe = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_RECV_PDU: nvmet_tcp_try_recv_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_DONE_RECV_PDU: nvmet_tcp_done_recv_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_EXEC_READ_REQ: nvmet_tcp_exec_read_req = entry->timestamp;
//                 break;
//             case NVMET_TCP_QUEUE_RESPONSE: nvmet_tcp_queue_response = entry->timestamp;
//                 break;
//             case NVMET_TCP_SETUP_C2H_DATA_PDU: nvmet_tcp_setup_c2h_data_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_DATA_PDU: nvmet_tcp_try_send_data_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_DATA: nvmet_tcp_try_send_data = entry->timestamp;
//                 break;
//             case NVMET_TCP_SETUP_RESPONSE_PDU: nvmet_tcp_setup_response_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_RESPONSE: nvmet_tcp_try_send_response = entry->timestamp;
//                 break;
//         }
//     }
//
//     breakdown->cnt++;
//     // Calculate breakdown latencies
//     breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;
//
//     breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send - nvme_tcp_queue_request;
//     breakdown->nvme_tcp_request_processing += nvme_tcp_try_send_cmd_pdu - nvme_tcp_try_send;
//
//     breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
//     breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_read_req - nvmet_tcp_done_recv_pdu;
//     breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response - nvmet_tcp_exec_read_req;
//     breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_c2h_data_pdu - nvmet_tcp_queue_response;
//     breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response - nvmet_tcp_setup_c2h_data_pdu;
//
//     breakdown->nvme_tcp_waiting += nvme_tcp_recv_skb1 - nvme_tcp_done_send_req;
//     breakdown->nvme_tcp_completion_queueing += nvme_tcp_recv_data - nvme_tcp_recv_skb1;
//     breakdown->nvme_tcp_response_processing += nvme_tcp_process_nvme_cqe - nvme_tcp_handle_c2h_data;
//
//     breakdown->blk_completion_queueing += blk_complete - nvme_tcp_process_nvme_cqe;
//
//     // End-to-end latencies
//     breakdown->blk_e2e += blk_complete - blk_submit;
//     breakdown->nvme_tcp_e2e += nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq;
//     breakdown->nvmet_tcp_e2e += nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu;
//
//     // Networking time
//     breakdown->networking += breakdown->nvme_tcp_waiting - breakdown->nvmet_tcp_e2e;
// }
//
// void break_latency_write_s(struct profile_record *record, struct write_breakdown_s *breakdown) {
//     struct ts_entry *entry = record->ts;
//     unsigned long long blk_submit = 0,
//             blk_complete = 0,
//             nvme_tcp_queue_rq = 0,
//             nvme_tcp_queue_request = 0,
//             nvme_tcp_try_send = 0,
//             nvme_tcp_try_send_cmd_pdu = 0,
//             nvme_tcp_try_send_data = 0,
//             nvme_tcp_done_send_req = 0,
//             nvmet_tcp_try_recv_pdu = 0,
//             nvmet_tcp_done_recv_pdu = 0,
//             nvmet_tcp_try_recv_data = 0,
//             nvmet_tcp_exec_write_req = 0,
//             nvmet_tcp_queue_response = 0,
//             nvmet_tcp_setup_response_pdu = 0,
//             nvmet_tcp_try_send_response = 0,
//             nvme_tcp_recv_skb = 0,
//             nvme_tcp_process_nvme_cqe = 0;
//
//     // Traverse the timestamp list and map events to variables
//     list_for_each_entry(entry, &record->ts->list, list) {
//         switch (entry->event) {
//             case BLK_SUBMIT: blk_submit = entry->timestamp;
//                 break;
//             case BLK_RQ_COMPLETE: blk_complete = entry->timestamp;
//                 break;
//             case NVME_TCP_QUEUE_RQ: nvme_tcp_queue_rq = entry->timestamp;
//                 break;
//             case NVME_TCP_QUEUE_REQUEST: nvme_tcp_queue_request = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND: nvme_tcp_try_send = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND_CMD_PDU: nvme_tcp_try_send_cmd_pdu = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND_DATA: nvme_tcp_try_send_data = entry->timestamp;
//                 break;
//             case NVME_TCP_DONE_SEND_REQ: nvme_tcp_done_send_req = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_RECV_PDU: nvmet_tcp_try_recv_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_DONE_RECV_PDU: nvmet_tcp_done_recv_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_RECV_DATA: nvmet_tcp_try_recv_data = entry->timestamp;
//                 break;
//             case NVMET_TCP_EXEC_WRITE_REQ: nvmet_tcp_exec_write_req = entry->timestamp;
//                 break;
//             case NVMET_TCP_QUEUE_RESPONSE: nvmet_tcp_queue_response = entry->timestamp;
//                 break;
//             case NVMET_TCP_SETUP_RESPONSE_PDU: nvmet_tcp_setup_response_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_RESPONSE: nvmet_tcp_try_send_response = entry->timestamp;
//                 break;
//             case NVME_TCP_RECV_SKB: nvme_tcp_recv_skb = entry->timestamp;
//                 break;
//             case NVME_TCP_PROCESS_NVME_CQE: nvme_tcp_process_nvme_cqe = entry->timestamp;
//                 break;
//         }
//     }
//
//     breakdown->cnt++;
//     // Calculate breakdown latencies
//     breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;
//
//     breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send - nvme_tcp_queue_request;
//     breakdown->nvme_tcp_request_processing += nvme_tcp_done_send_req - nvme_tcp_try_send;
//
//     breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
//     breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_write_req - nvmet_tcp_done_recv_pdu;
//     breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response - nvmet_tcp_exec_write_req;
//     breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_response_pdu - nvmet_tcp_queue_response;
//     breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response - nvmet_tcp_setup_response_pdu;
//
//     breakdown->nvme_tcp_waiting_for_resp += nvme_tcp_recv_skb - nvme_tcp_done_send_req;
//     breakdown->nvme_tcp_completion_queueing += nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb;
//
//     breakdown->blk_completion_queueing += blk_complete - nvme_tcp_process_nvme_cqe;
//
//     // End-to-end latencies
//     breakdown->blk_e2e += blk_complete - blk_submit;
//     breakdown->nvme_tcp_e2e += nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq;
//     breakdown->nvmet_tcp_e2e += nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu;
//
//     // Networking time
//     breakdown->networking += breakdown->nvme_tcp_waiting_for_resp - breakdown->nvmet_tcp_e2e;
// }
//
// void break_latency_write_l(struct profile_record *record, struct write_breakdown_l *breakdown) {
//     struct ts_entry *entry = record->ts;
//     unsigned long long blk_submit = 0,
//
//             nvme_tcp_queue_rq = 0,
//             nvme_tcp_queue_request1 = 0,
//             nvme_tcp_try_send1 = 0,
//             nvme_tcp_try_send_cmd_pdu = 0,
//             nvme_tcp_done_send_req1 = 0,
//
//             nvmet_tcp_try_recv_pdu1 = 0,
//             nvmet_tcp_done_recv_pdu = 0,
//             nvmet_tcp_queue_response1 = 0,
//             nvmet_tcp_setup_r2t_pdu = 0,
//             nvmet_tcp_try_send_r2t = 0,
//
//             nvme_tcp_recv_skb1 = 0,
//             nvme_tcp_handle_r2t = 0,
//             nvme_tcp_queue_request2 = 0,
//             nvme_tcp_try_send2 = 0,
//             nvme_tcp_try_send_data_pdu = 0,
//             nvme_tcp_try_send_data = 0,
//             nvme_tcp_done_send_req2 = 0,
//
//             nvmet_tcp_try_recv_pdu2 = 0,
//             nvmet_tcp_handle_h2c_data_pdu = 0,
//             nvmet_tcp_try_recv_data = 0,
//             nvmet_tcp_exec_write_req = 0,
//             nvmet_tcp_queue_response2 = 0,
//             nvmet_tcp_setup_response_pdu = 0,
//             nvmet_tcp_try_send_response = 0,
//
//             nvme_tcp_recv_skb2 = 0,
//             nvme_tcp_process_nvme_cqe = 0,
//
//             blk_rq_complete = 0;
//
//     int nvme_tcp_queue_request_cnt = 0;
//     int nvme_tcp_try_send_cnt = 0;
//     int nvme_tcp_done_send_req_cnt = 0;
//     int nvmet_tcp_try_recv_pdu_cnt = 0;
//     int nvmet_tcp_queue_response_cnt = 0;
//     int nvme_tcp_recv_skb_cnt = 0;
//
//     // Traverse the timestamp list and map events to variables
//     list_for_each_entry(entry, &record->ts->list, list) {
//         switch (entry->event) {
//             case BLK_SUBMIT: blk_submit = entry->timestamp;
//             break;
//             case BLK_RQ_COMPLETE: blk_rq_complete = entry->timestamp;
//             break;
//             case NVME_TCP_QUEUE_RQ: nvme_tcp_queue_rq = entry->timestamp;
//                 break;
//
//             case NVME_TCP_QUEUE_REQUEST:
//                 if (nvme_tcp_queue_request_cnt == 0) {
//                     nvme_tcp_queue_request1 = entry->timestamp;
//                 } else {
//                     nvme_tcp_queue_request2 = entry->timestamp;
//                 }
//                 nvme_tcp_queue_request_cnt++;
//                 break;
//
//             case NVME_TCP_TRY_SEND:
//                 if (nvme_tcp_try_send_cnt == 0) {
//                     nvme_tcp_try_send1 = entry->timestamp;
//                 } else {
//                     nvme_tcp_try_send2 = entry->timestamp;
//                 }
//                 nvme_tcp_try_send_cnt++;
//                 break;
//
//             case NVME_TCP_TRY_SEND_CMD_PDU: nvme_tcp_try_send_cmd_pdu = entry->timestamp;
//                 break;
//
//             case NVME_TCP_DONE_SEND_REQ:
//                 if (nvme_tcp_done_send_req_cnt == 0) {
//                     nvme_tcp_done_send_req1 = entry->timestamp;
//                 } else {
//                     nvme_tcp_done_send_req2 = entry->timestamp;
//                 }
//                 nvme_tcp_done_send_req_cnt++;
//                 break;
//
//             case NVMET_TCP_TRY_RECV_PDU:
//                 if (nvmet_tcp_try_recv_pdu_cnt == 0) {
//                     nvmet_tcp_try_recv_pdu1 = entry->timestamp;
//                 } else {
//                     nvmet_tcp_try_recv_pdu2 = entry->timestamp;
//                 }
//                 nvmet_tcp_try_recv_pdu_cnt++;
//                 break;
//
//             case NVMET_TCP_DONE_RECV_PDU: nvmet_tcp_done_recv_pdu = entry->timestamp;
//                 break;
//
//             case NVMET_TCP_QUEUE_RESPONSE:
//                 if (nvmet_tcp_queue_response_cnt == 0) {
//                     nvmet_tcp_queue_response1 = entry->timestamp;
//                 } else {
//                     nvmet_tcp_queue_response2 = entry->timestamp;
//                 }
//                 nvmet_tcp_queue_response_cnt++;
//                 break;
//
//             case NVMET_TCP_SETUP_R2T_PDU: nvmet_tcp_setup_r2t_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_R2T: nvmet_tcp_try_send_r2t = entry->timestamp;
//                 break;
//             case NVME_TCP_HANDLE_R2T: nvme_tcp_handle_r2t = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND_DATA_PDU: nvme_tcp_try_send_data_pdu = entry->timestamp;
//                 break;
//             case NVME_TCP_TRY_SEND_DATA: nvme_tcp_try_send_data = entry->timestamp;
//                 break;
//             case NVMET_TCP_HANDLE_H2C_DATA_PDU: nvmet_tcp_handle_h2c_data_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_RECV_DATA: nvmet_tcp_try_recv_data = entry->timestamp;
//                 break;
//             case NVMET_TCP_EXEC_WRITE_REQ: nvmet_tcp_exec_write_req = entry->timestamp;
//                 break;
//             case NVMET_TCP_SETUP_RESPONSE_PDU: nvmet_tcp_setup_response_pdu = entry->timestamp;
//                 break;
//             case NVMET_TCP_TRY_SEND_RESPONSE: nvmet_tcp_try_send_response = entry->timestamp;
//                 break;
//             case NVME_TCP_RECV_SKB:
//                 if (nvme_tcp_recv_skb_cnt == 0) {
//                     nvme_tcp_recv_skb1 = entry->timestamp;
//                 } else {
//                     nvme_tcp_recv_skb2 = entry->timestamp;
//                 }
//                 nvme_tcp_recv_skb_cnt++;
//                 break;
//             case NVME_TCP_PROCESS_NVME_CQE: nvme_tcp_process_nvme_cqe = entry->timestamp;
//                 break;
//         }
//     }
//     breakdown->cnt++;
//     // Calculate breakdown latencies
//     breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;
//
//     breakdown->nvme_tcp_cmd_submission_queueing += nvme_tcp_try_send1 - nvme_tcp_queue_request1;
//     breakdown->nvme_tcp_cmd_capsule_processing += nvme_tcp_try_send_cmd_pdu - nvme_tcp_try_send1;
//
//     breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu1;
//     breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_queue_response1 - nvmet_tcp_done_recv_pdu;
//     breakdown->nvmet_tcp_r2t_submission_queueing += nvmet_tcp_setup_r2t_pdu - nvmet_tcp_queue_response1;
//     breakdown->nvmet_tcp_r2t_resp_processing += nvmet_tcp_try_send_r2t - nvmet_tcp_setup_r2t_pdu;
//
//     breakdown->nvme_tcp_wait_for_r2t += nvme_tcp_recv_skb1 - nvme_tcp_try_send_cmd_pdu;
//     breakdown->nvme_tcp_r2t_resp_queueing += nvme_tcp_handle_r2t - nvme_tcp_recv_skb1;
//     breakdown->nvme_tcp_data_pdu_submission_queueing += nvme_tcp_try_send_data_pdu - nvme_tcp_handle_r2t;
//     breakdown->nvme_tcp_data_pdu_processing += nvme_tcp_done_send_req2 - nvme_tcp_try_send_data;
//
//     breakdown->nvmet_tcp_wait_for_data += nvmet_tcp_try_recv_pdu2 - nvmet_tcp_try_send_r2t;
//     breakdown->nvmet_tcp_write_cmd_queueing += nvmet_tcp_handle_h2c_data_pdu - nvmet_tcp_try_recv_pdu2;
//     breakdown->nvmet_tcp_write_cmd_processing += nvmet_tcp_exec_write_req - nvmet_tcp_handle_h2c_data_pdu;
//     breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response2 - nvmet_tcp_exec_write_req;
//     breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_response_pdu - nvmet_tcp_queue_response2;
//     breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response - nvmet_tcp_setup_response_pdu;
//
//     breakdown->nvme_tcp_wait_for_resp += nvme_tcp_recv_skb2 - nvme_tcp_done_send_req2;
//     breakdown->nvme_tcp_resp_queueing += nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb2;
//
//     breakdown->blk_completion_queueing += blk_rq_complete - nvme_tcp_process_nvme_cqe;
//
//     // End-to-end latencies
//     breakdown->blk_e2e += blk_rq_complete - blk_submit;
//     breakdown->networking += breakdown->nvme_tcp_wait_for_r2t + breakdown->nvme_tcp_wait_for_resp -
//             (nvmet_tcp_try_send_r2t - nvmet_tcp_try_recv_pdu1) - (
//                 nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu2);
// }
//
//
// void analyze_statistics(struct per_core_statistics *stat, struct ntprof_config *config) {
//     int i;
//     struct task_struct *threads[MAX_CORE_NUM];
//
//     // update global config, report part
//     global_config.enable_group_by_session = config->enable_group_by_session;
//     global_config.enable_group_by_size = config->enable_group_by_size;
//     global_config.enable_group_by_type = config->enable_group_by_type;
//     global_config.enable_latency_distribution = config->enable_latency_distribution;
//     global_config.enable_throughput = config->enable_throughput;
//     global_config.enable_latency_breakdown = config->enable_latency_breakdown;
//     global_config.enable_queue_length = config->enable_queue_length;
//
//     // 1. map the records to different categories
//
//     for (i = 0; i < MAX_CORE_NUM; ++i) {
//         if (!list_empty(&stat[i].completed_records)) {
//             threads[i] = kthread_run(analyze_per_core_profile_records, &stat[i], "analyze_per_core_profile_records_%d",
//                                      i);
//         } else {
//             threads[i] = NULL;
//         }
//     }
//
//     for (i = 0; i < MAX_CORE_NUM; ++i) {
//         if (threads[i]) {
//             int ret = kthread_stop(threads[i]);
//             if (ret != 0) {
//                 pr_warn("Thread analyze_per_core_profile_records_%d did not exit cleanly\n", i);
//             }
//         }
//     }
//
//     // 2. reduce the categories to a single report
// }
