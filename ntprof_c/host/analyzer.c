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
#include <linux/spinlock.h>
#include <linux/sort.h>

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

// a collection of profile records, of the same type (identified by the key)
struct categorized_records {
  struct hlist_node hash_node;
  struct list_head records;
  struct category_key key;
  // multiple threads will insert new elements to a categorized_records
  atomic_t count;
};

// given a profiling record and the configuration, insert it to the hashtable
static int categorize_record(struct profile_record* record,
                             struct ntprof_config* config) {
  // generate the category key
  struct category_key key = {
      .io_size = config->enable_group_by_size ? record->metadata.size : -1,
      .io_type = config->enable_group_by_type ? record->metadata.is_write : -1,
  };
  const char* session = config->enable_group_by_session
                          ? config->session_name
                          : "";
  strncpy(key.session_name, session, MAX_SESSION_NAME_LEN - 1);
  key.session_name[MAX_SESSION_NAME_LEN - 1] = '\0';

  // check if the key exists, if not, try to create a new one
  u32 hash = jhash(&key, sizeof(key), 0);
  struct categorized_records* cat;
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
    // pr_cont("move [req->tag=%d, req->cmdid=%d], ", record->metadata.req_tag, record->metadata.cmdid);
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

      // pr_cont("move [req->tag=%d, req->cmdid=%d], ", record->metadata.req_tag, record->metadata.cmdid);
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
static void categorize_records_of_each_core(struct per_core_statistics* stat,
                                            int core_id) {
  struct profile_record *rec, *tmp;
  unsigned long flags;
  // TODO: spinlock?
  pr_cont("categorize: ");
  list_for_each_entry_safe(rec, tmp, &stat->completed_records, list) {
    categorize_record(rec, &global_config);
  }
  // remove all elements in the incompleted_records
  list_for_each_entry_safe(rec, tmp, &stat->incomplete_records, list) {
    // pr_cont("remove [req->tag=%d, req->cmdid=%d], ", rec->metadata.req_tag,
    // rec->metadata.cmdid);
    list_del(&rec->list);
    kfree(rec);
  }
  pr_info("");
}

struct analysis_work {
  struct work_struct work;
  struct per_core_statistics* stat;
  int core_id;
};

static void categorize_work_fn(struct work_struct* work) {
  // pr_info("categorize_work_fn start working");
  struct analysis_work* w = container_of(work, struct analysis_work, work);
  categorize_records_of_each_core(w->stat, w->core_id);
  kfree(w);
}

void start_phase_1(struct per_core_statistics* stat,
                   struct ntprof_config* config) {
  int i;
  struct workqueue_struct* wq;

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

    struct analysis_work* work = kmalloc(sizeof(*work), GFP_KERNEL);
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


static const u32 quantiles_scaled[MAX_DIST_BUCKET] = {
    100, 500, 700, 800, 900, 950, 990, 999
};

struct sample_array {
  int* data;
  int count;
  int capacity;
};


static void init_sample_array(struct sample_array* arr, int total_cnt) {
  arr->data = kmalloc(total_cnt * sizeof(int), GFP_KERNEL);
  if (!arr->data) {
    pr_err("Memory allocation failed for sample_array\n");
  }
  arr->count = 0;
  arr->capacity = total_cnt;
}


static void add_sample(struct sample_array* arr, int total_cnt) {
  if (arr->count >= arr->capacity) {
    pr_warn("Too many samples in the array\n");
    return;
  }
  arr->data[arr->count++] = total_cnt;
}

static void free_sample_array(struct sample_array* arr) {
  kfree(arr->data);
  arr->data = NULL;
  arr->count = 0;
  arr->capacity = 0;
}

static int compare_int(const void* a, const void* b) {
  return (*(int*)a - *(int*)b);
}

static void calculate_quantiles(const struct sample_array* samples,
                                int dest[MAX_DIST_BUCKET]) {
  if (samples->count == 0) {
    memset(dest, 0, MAX_DIST_BUCKET * sizeof(int));
    return;
  }

  if (samples->count == 1) {
    for (int i = 0; i < MAX_DIST_BUCKET; ++i) {
      dest[i] = samples->data[0];
    }
    return;
  }

  sort(samples->data, samples->count, sizeof(int), compare_int, NULL);

  for (int i = 0; i < MAX_DIST_BUCKET; ++i) {
    u64 scaled_pos = (u64)quantiles_scaled[i] * (samples->count - 1);
    u32 idx_low = (u32)(scaled_pos / 1000);
    u32 frac = (u32)(scaled_pos % 1000);

    if (idx_low >= samples->count - 1) {
      dest[i] = samples->data[samples->count - 1];
    } else {
      int low = samples->data[idx_low];
      int high = samples->data[idx_low + 1];
      dest[i] = low + ((high - low) * frac + 500) / 1000;
    }
  }
}


void summarize_category(struct categorized_records* cat,
                        struct category_summary* cs) {
  struct profile_record *record, *tmp;
  cs->key = cat->key;

  pr_info("before summarization");
  print_latency_breakdown_summary(&cs->lbs);

  struct {
    struct sample_array blk_submission;
    struct sample_array blk_completion;
    struct sample_array nvme_tcp_submission;
    struct sample_array nvme_tcp_completion;
    struct sample_array nvmet_tcp_submission;
    struct sample_array nvmet_tcp_completion;
    struct sample_array target_subsystem;
    struct sample_array nstack_submission;
    struct sample_array nstack_completion;
    struct sample_array network_transmission;
  } samples;

  int sample_cnt = atomic_read(&cat->count);
  init_sample_array(&samples.blk_submission, sample_cnt);
  init_sample_array(&samples.blk_completion, sample_cnt);
  init_sample_array(&samples.nvme_tcp_submission, sample_cnt);
  init_sample_array(&samples.nvme_tcp_completion, sample_cnt);
  init_sample_array(&samples.nvmet_tcp_submission, sample_cnt);
  init_sample_array(&samples.nvmet_tcp_completion, sample_cnt);
  init_sample_array(&samples.target_subsystem, sample_cnt);
  init_sample_array(&samples.nstack_submission, sample_cnt);
  init_sample_array(&samples.nstack_completion, sample_cnt);
  init_sample_array(&samples.network_transmission, sample_cnt);

  pr_info("sample_cnt = %d\n", sample_cnt);

  list_for_each_entry_safe(record, tmp, &cat->records, list) {
    list_del(&record->list); // Remove from list
    if (is_valid_profile_record(record) == 0) {
      print_profile_record(record);
    } else {
      // // print_profile_record(record);
      struct latency_breakdown bd = {0};
      break_latency(record, &bd);
      //
      cs->lbs.cnt++;

      cs->lbs.bd_sum.blk_submission += bd.blk_submission;
      cs->lbs.bd_sum.blk_completion += bd.blk_completion;
      cs->lbs.bd_sum.nvme_tcp_submission += bd.nvme_tcp_submission;
      cs->lbs.bd_sum.nvme_tcp_completion += bd.nvme_tcp_completion;
      cs->lbs.bd_sum.nvmet_tcp_submission += bd.nvmet_tcp_submission;
      cs->lbs.bd_sum.nvmet_tcp_completion += bd.nvmet_tcp_completion;
      cs->lbs.bd_sum.target_subsystem += bd.target_subsystem;
      cs->lbs.bd_sum.nstack_submission += bd.nstack_submission;
      cs->lbs.bd_sum.nstack_completion += bd.nstack_completion;
      cs->lbs.bd_sum.network_transmission += bd.network_transmission;
      //
      add_sample(&samples.blk_submission, bd.blk_submission);
      add_sample(&samples.blk_completion, bd.blk_completion);
      add_sample(&samples.nvme_tcp_submission, bd.nvme_tcp_submission);
      add_sample(&samples.nvme_tcp_completion, bd.nvme_tcp_completion);
      add_sample(&samples.nvmet_tcp_submission, bd.nvmet_tcp_submission);
      add_sample(&samples.nvmet_tcp_completion, bd.nvmet_tcp_completion);
      add_sample(&samples.target_subsystem, bd.target_subsystem);
      add_sample(&samples.nstack_submission, bd.nstack_submission);
      add_sample(&samples.nstack_completion, bd.nstack_completion);
      add_sample(&samples.network_transmission, bd.network_transmission);

      // print_breakdown(&cs->bd);
    }
    kfree(record);
  }
  pr_info("cs->lbs.cnt: %d", cs->lbs.cnt);
  pr_info("cs->lbs.bd_sum.blk_submission: %lld", cs->lbs.bd_sum.blk_submission);
  pr_info("cs->lbs.bd_sum.blk_completion: %lld", cs->lbs.bd_sum.blk_completion);
  pr_info("cs->lbs.bd_sum.nvme_tcp_submission: %lld",
          cs->lbs.bd_sum.nvme_tcp_submission);
  pr_info("cs->lbs.bd_sum.nvme_tcp_completion: %lld",
          cs->lbs.bd_sum.nvme_tcp_completion);
  pr_info("cs->lbs.bd_sum.nvmet_tcp_submission: %lld",
          cs->lbs.bd_sum.nvmet_tcp_submission);
  pr_info("cs->lbs.bd_sum.nvmet_tcp_completion: %lld",
          cs->lbs.bd_sum.nvmet_tcp_completion);
  pr_info("cs->lbs.bd_sum.target_subsystem: %lld",
          cs->lbs.bd_sum.target_subsystem);
  pr_info("cs->lbs.bd_sum.nstack_submission: %lld",
          cs->lbs.bd_sum.nstack_submission);
  pr_info("cs->lbs.bd_sum.nstack_completion: %lld",
          cs->lbs.bd_sum.nstack_completion);
  pr_info("cs->lbs.bd_sum.network_transmission: %lld",
          cs->lbs.bd_sum.network_transmission);

  calculate_quantiles(&samples.blk_submission, cs->lbs.dist.blk_submission);
  calculate_quantiles(&samples.blk_completion, cs->lbs.dist.blk_completion);
  calculate_quantiles(&samples.nvme_tcp_submission,
                      cs->lbs.dist.nvme_tcp_submission);
  calculate_quantiles(&samples.nvme_tcp_completion,
                      cs->lbs.dist.nvme_tcp_completion);
  calculate_quantiles(&samples.nvmet_tcp_submission,
                      cs->lbs.dist.nvmet_tcp_submission);
  calculate_quantiles(&samples.nvmet_tcp_completion,
                      cs->lbs.dist.nvmet_tcp_completion);
  calculate_quantiles(&samples.target_subsystem, cs->lbs.dist.target_subsystem);
  calculate_quantiles(&samples.nstack_submission,
                      cs->lbs.dist.nstack_submission);
  calculate_quantiles(&samples.nstack_completion,
                      cs->lbs.dist.nstack_completion);
  calculate_quantiles(&samples.network_transmission,
                      cs->lbs.dist.network_transmission);

  free_sample_array(&samples.blk_submission);
  free_sample_array(&samples.blk_completion);
  free_sample_array(&samples.nvme_tcp_submission);
  free_sample_array(&samples.nvme_tcp_completion);
  free_sample_array(&samples.nvmet_tcp_submission);
  free_sample_array(&samples.nvmet_tcp_completion);
  free_sample_array(&samples.target_subsystem);
  free_sample_array(&samples.nstack_submission);
  free_sample_array(&samples.nstack_completion);
  free_sample_array(&samples.network_transmission);

  pr_info("after summarization");
  print_latency_breakdown_summary(&cs->lbs);
}

struct summarize_work {
  struct work_struct work;
  struct categorized_records* cat;
  struct report* global_result;
  int id;
};

// this whole function is protected by the category_mutex
// static void merge_result(struct report *global, struct report *local, int idx) {
//     global->total_io += local->total_io;
//     global->breakdown[idx] = local->breakdown[idx];
// }

static void summarize_work_fn(struct work_struct* work) {
  pr_debug("summarize_work_fn start working!");
  struct summarize_work* sw = container_of(work, struct summarize_work, work);
  struct categorized_records* cat = sw->cat;

  summarize_category(cat, &sw->global_result->summary[sw->id]);

  // pr_info("Category => %s, %d, %d", cat->key.session_name, cat->key.io_size, cat->key.io_type);
  // it it is writel, print the breakdown
  // print_breakdown(&sw->global_result->summary[sw->id].bd);

  mutex_lock(&category_mutex);
  // udpate the global report
  // merge_result(sw->global_result, &(sw->local_result), id);
  hash_del(&cat->hash_node);
  kfree(cat); // Free category after summarization
  mutex_unlock(&category_mutex);

  kfree(sw);
}

static void start_phase_2(struct report* result) {
  struct workqueue_struct* wq = alloc_workqueue("summary_wq", WQ_UNBOUND, 0);

  if (!wq) {
    pr_warn("Failed to create summary workqueue\n");
    return;
  }

  mutex_lock(&category_mutex);

  int bkt;
  struct hlist_node* tmp;
  struct categorized_records* cat;

  int cnt = 0;
  hash_for_each_safe(categories_hash, bkt, tmp, cat, hash_node) {
    if (cnt >= MAX_CATEGORIES) {
      pr_warn("Too many categories, skip the rest\n");
      break;
    }
    struct summarize_work* sw = kmalloc(sizeof(*sw), GFP_KERNEL);
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
  result->category_num = cnt;

  mutex_unlock(&category_mutex);

  flush_workqueue(wq);
  destroy_workqueue(wq);

  // pr_info("after phase 2, the cnt is %d", cnt);
  // if (cnt > 0) {
  // print_breakdown(&result->summary[0].bd);
  // }
}

// Initialization and cleanup functions
static void preparing_analyzation(void) {
  hash_init(categories_hash);
  atomic_set(&categories_count, 0);
}

static void finish_analyzation(void) {
  int bkt;
  struct hlist_node* tmp;
  struct categorized_records* cat;

  mutex_lock(&category_mutex);
  hash_for_each_safe(categories_hash, bkt, tmp, cat, hash_node) {
    hash_del(&cat->hash_node);
    // if the category is not empty, free the profiling records
    struct profile_record *rec, *tmp;
    list_for_each_entry_safe(rec, tmp, &cat->records, list) {
      list_del(&rec->list);
      kfree(rec);
    }
    kfree(cat);
  }
  mutex_unlock(&category_mutex);
}

void analyze(struct ntprof_config* conf, struct report* rpt) {
  pr_debug("start preparing analysis phase!");
  preparing_analyzation();

  pr_debug("start phase 1: categorize the profile records");
  start_phase_1(stat, conf);

  pr_debug("start phase 2: summarize the profile records in each category");
  start_phase_2(rpt);

  pr_debug("finish analysis, clean up the hashmap");
  finish_analyzation();

  // pr_warn("start removing!");
  // int i;
  // long long cnt = 0;
  // for (i = 0; i < MAX_CORE_NUM; i++) {
  //   struct per_core_statistics* s = &stat[i];
  //   struct profile_record *rec, *tmp;
  //   SPINLOCK_IRQSAVE_DISABLEPREEMPT_Q(&stat[i].lock, "analyze", i);
  //
  //   list_for_each_entry_safe(rec, tmp, &s->completed_records, list) {
  //     // remove all elements
  //     list_del(&rec->list);
  //     kfree(rec);
  //     cnt ++;
  //   }
  //   list_for_each_entry_safe(rec, tmp, &s->incomplete_records, list) {
  //     // remove all elements
  //     list_del(&rec->list);
  //     kfree(rec);
  //   }
  //   SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_Q(&stat[i].lock, "analyze", i);
  // }
  // pr_info("total_records: %llu", cnt);
}