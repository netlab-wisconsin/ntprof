#ifndef _K_BLK_LAYER_H_
#define _K_BLK_LAYER_H_

#include <linux/atomic.h>
#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kdev_t.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <trace/events/block.h>

#include "k_ntm.h"
#include "util.h"

atomic64_t blk_sample_cnt;

bool blk_to_sample(void) {
  return atomic64_inc_return(&blk_sample_cnt) % args->rate == 0;
}

/**
 * blk layer statistics, with atomic variables
 * This is used for recording raw data in the kernel space
 */
struct _blk_stat {
  /** total number of read io */
  atomic64_t read_count;
  /** total number of write io */
  atomic64_t write_count;
  /**
   * read io number of different sizes
   * the sizs are divided into 9 categories
   * refers to enum size_type in ntm_com.h
   */
  atomic64_t read_io[9];
  /** write io number of different sizes */
  atomic64_t write_io[9];

  /** overall read and write lat */
  atomic64_t read_lat;
  atomic64_t write_lat;

  /** read and write lat for different type of io */
  atomic64_t read_io_lat[9];
  atomic64_t write_io_lat[9];

  /** TODO: number of io in-flight */
  atomic64_t pending_rq;
};

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
 */
void inc_cnt_atomic_arr(atomic64_t *arr, int size) {
  atomic64_inc(&arr[size_to_enum(size)]);
}

void add_atomic_array(atomic64_t *arr, int size, u64 val) {
  atomic64_add(val, &arr[size_to_enum(size)]);
}

/**
 * initialize a _blk_stat structure
 * set all atomic variables to 0
 * @param tr: the _blk_stat to be initialized
 */
void _init_blk_tr(struct _blk_stat *tr) {
  int i;
  atomic64_set(&tr->read_count, 0);
  atomic64_set(&tr->write_count, 0);
  atomic64_set(&tr->pending_rq, 0);
  atomic64_set(&tr->read_lat, 0);
  atomic64_set(&tr->write_lat, 0);
  for (i = 0; i < 9; i++) {
    atomic64_set(&tr->read_io[i], 0);
    atomic64_set(&tr->write_io[i], 0);
    atomic64_set(&tr->read_io_lat[i], 0);
    atomic64_set(&tr->write_io_lat[i], 0);
  }
}

/**
 * copy the data from _blk_stat to blk_stat
 * This function is not thread safe
 */
void copy_blk_stat(struct blk_stat *dst, struct _blk_stat *src) {
  int i;
  dst->read_count = atomic64_read(&src->read_count);
  dst->write_count = atomic64_read(&src->write_count);
  for (i = 0; i < 9; i++) {
    dst->read_io[i] = atomic64_read(&src->read_io[i]);
    dst->write_io[i] = atomic64_read(&src->write_io[i]);
    dst->read_io_lat[i] = atomic64_read(&src->read_io_lat[i]);
    dst->write_io_lat[i] = atomic64_read(&src->write_io_lat[i]);
  }
  dst->read_lat = atomic64_read(&src->read_lat);
  dst->write_lat = atomic64_read(&src->write_lat);
}

/** in-kernel strucutre to update blk layer statistics */
static struct _blk_stat *inner_blk_stat;

/** raw blk layer statistics, shared in user space*/
// static struct blk_stat *raw_blk_stat;
static struct shared_blk_layer_stat *shared_blk_layer_stat;

/**
 * communication entries
 */
struct proc_dir_entry *entry_blk_dir;
struct proc_dir_entry *entry_blk_stat;

// /**
//  * TODO: get the number of pending requests in the queue
//  */
// unsigned int get_pending_requests(struct request_queue *q) { return 0; }

// /**
//  * This function is triggered when a bio is queued. This is the main function
//  to
//  * trace the io in the blk layer.
//  * @param ignore:
//  * @param bio: the bio to be traced
//  */
// void on_block_bio_queue(void *ignore, struct bio *bio) { return; }

void on_block_rq_complete(void *ignore, struct request *rq, int err,
                          unsigned int nr_bytes) {
  // if (ctrl && args->io_type + rq_data_dir(rq) != 1) {
  //   /** if the name of the block device is unexpected, do not record any info */
  //   // pr_info("on_block_rq_complete: req type=%d \n", rq_data_dir(rq));
  //   if (rq->bio == NULL || rq->bio->bi_bdev == NULL ||
  //       rq->bio->bi_bdev->bd_disk == NULL)
  //     return;
  //   struct bio *bio = rq->bio;
  //   if (!is_same_dev_name(bio->bi_bdev->bd_disk->disk_name, args->dev)) return;
  //   while (bio) {
  //     if (blk_to_sample()) {
  //       /** update inner_blk_stat to record all sampled bio */
  //       u64 _start = bio_issue_time(&bio->bi_issue);
  //       u64 _now = __bio_issue_time(ktime_get_ns());
  //       u64 lat = _now - _start;

  //       /** read the size of the io */
  //       unsigned int size = bio->bi_iter.bi_size;

  //       /** read the io direction and increase the corresponding counter */
  //       if (bio_data_dir(bio) == READ) {
  //         atomic64_inc(&inner_blk_stat->read_count);
  //         inc_cnt_atomic_arr(inner_blk_stat->read_io, size);
  //         add_atomic_array(inner_blk_stat->read_io_lat, size, lat);
  //         atomic64_add(lat, &inner_blk_stat->read_lat);
  //       } else if (bio_data_dir(bio) == WRITE) {
  //         inc_cnt_atomic_arr(inner_blk_stat->write_io, size);
  //         atomic64_inc(&inner_blk_stat->write_count);
  //         atomic64_add(lat, &inner_blk_stat->write_lat);
  //         add_atomic_array(inner_blk_stat->write_io_lat, size, lat);
  //       } else {
  //         pr_err("Unexpected bio direction, neither READ nor WRITE.\n");
  //       }
  //     }
  //     bio = bio->bi_next;
  //   }
  // }
}

void on_block_bio_complete(void *ignore, struct request_queue *q,
                           struct bio *bio) {
  if (ctrl && args->io_type + bio_data_dir(bio) != 1) {
    /** if the name of the block device is unexpected, do not record any info */
    // pr_info("on_block_rq_complete: req type=%d \n", rq_data_dir(rq));
    if (bio == NULL || bio->bi_bdev == NULL || bio->bi_bdev->bd_disk == NULL)
      return;
    // pr_info("dev: %s", bio->bi_bdev->bd_disk->disk_name); -- dev: nvme4c4n1
    if (!is_same_dev_name(bio->bi_bdev->bd_disk->disk_name, args->dev)) return;

    if (blk_to_sample()) {
      /** update inner_blk_stat to record all sampled bio */
      u64 _start = bio_issue_time(&bio->bi_issue);
      u64 _now = __bio_issue_time(ktime_get_ns());
      u64 lat = _now - _start;

      /** read the size of the io */
      unsigned int size = bio->bi_iter.bi_size;

      /** read the io direction and increase the corresponding counter */
      if (bio_data_dir(bio) == READ) {
        atomic64_inc(&inner_blk_stat->read_count);
        inc_cnt_atomic_arr(inner_blk_stat->read_io, size);
        add_atomic_array(inner_blk_stat->read_io_lat, size, lat);
        atomic64_add(lat, &inner_blk_stat->read_lat);
      } else if (bio_data_dir(bio) == WRITE) {
        inc_cnt_atomic_arr(inner_blk_stat->write_io, size);
        atomic64_inc(&inner_blk_stat->write_count);
        atomic64_add(lat, &inner_blk_stat->write_lat);
        add_atomic_array(inner_blk_stat->write_io_lat, size, lat);
      } else {
        pr_err("Unexpected bio direction, neither READ nor WRITE.\n");
      }
    }
    bio = bio->bi_next;
  }
}

/**
 * register the tracepoints
 * - block_bio_queue
 */
static int blk_register_tracepoints(void) {
  int ret;
  ret = register_trace_block_rq_complete(on_block_rq_complete, NULL);
  if (ret) goto failed;

  ret = register_trace_block_bio_complete(on_block_bio_complete, NULL);
  if (ret) goto unregister_block_rq_complete;
  return 0;

unregister_block_rq_complete:
  unregister_trace_block_rq_complete(on_block_rq_complete, NULL);

failed:
  return ret;
}

/**
 * unregister the tracepoints
 * - block_bio_queue
 */
static void blk_unregister_tracepoints(void) {
  unregister_trace_block_rq_complete(on_block_rq_complete, NULL);
  unregister_trace_block_bio_complete(on_block_bio_complete, NULL);
}

/**
 * This function defines how user space map the a variable to
 * the kernel space variable, raw_blk_stat
 * @param filp: the file to map
 * @param vma: the virtual memory area to map
 * @return 0 if success, -EAGAIN if failed
 */
static int mmap_shared_blk_layer_stat(struct file *filp,
                                      struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(shared_blk_layer_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

/**
 * define the operations for the raw_blk_stat command
 * - proc_mmap: map the raw_blk_stat to the user space
 */
static const struct proc_ops ntm_shared_blk_layer_stat_ops = {
    .proc_mmap = mmap_shared_blk_layer_stat,
};

/**
 * update the blk layer statistic periodically
 * This function will be triggered in the main routine thread
 */
void blk_layer_update(u64 now) {
  /** update the raw blk layer statistic in the user space */
  copy_blk_stat(&shared_blk_layer_stat->all_time_stat, inner_blk_stat);
}

/**
 * initialize the variables
 * - set ctrl to 0
 * - set device_name to empty string
 * - allocate memory for _raw_blk_stat, raw_blk_stat, sw, sample
 * - initialize the blk_stat structures
 */
int init_blk_layer_variables(void) {
  atomic64_set(&blk_sample_cnt, 0);
  /** _raw_blk_stat */
  inner_blk_stat = kmalloc(sizeof(*inner_blk_stat), GFP_KERNEL);
  if (!inner_blk_stat) return -ENOMEM;
  _init_blk_tr(inner_blk_stat);

  /** shared_blk_layer_stat */
  shared_blk_layer_stat = vmalloc(sizeof(*shared_blk_layer_stat));
  if (!shared_blk_layer_stat) return -ENOMEM;
  init_blk_tr(&shared_blk_layer_stat->all_time_stat);

  return 0;
}

int free_blk_layer_variables(void) {
  if (inner_blk_stat) kfree(inner_blk_stat);
  if (shared_blk_layer_stat) vfree(shared_blk_layer_stat);
  return 0;
}

/**
 * initialize the proc entries
 * - create /proc/ntm/blk dir
 * - create /proc/ntm/blk/stat entry
 *
 * make sure if one of the proc entries is not created, remove the previous
 * created entries
 */
int init_blk_layer_proc_entries(void) {
  entry_blk_dir = proc_mkdir("blk", parent_dir);
  if (!entry_blk_dir) goto failed;

  entry_blk_stat =
      proc_create("stat", 0666, entry_blk_dir, &ntm_shared_blk_layer_stat_ops);
  if (!entry_blk_stat) goto remove_blk_dir;

  return 0;

remove_blk_dir:
  remove_proc_entry("blk", parent_dir);
  entry_blk_dir = NULL;
failed:
  return -ENOMEM;
}

/**
 * remove the proc entries
 * - remove /proc/ntm/stat
 * - remove /proc/ntm/blk dir
 */
static void remove_blk_layer_proc_entries(void) {
  if (entry_blk_stat) remove_proc_entry("stat", entry_blk_dir);
  if (entry_blk_dir) remove_proc_entry("blk", parent_dir);
}

/**
 * initialize the ntm blk layer
 * - initialize the variables
 * - initialize the proc entries
 * - register the tracepoints
 */
static int init_blk_layer_monitor(void) {
  int ret;
  ret = init_blk_layer_variables();
  if (ret) return ret;
  ret = init_blk_layer_proc_entries();
  if (ret) return ret;

  ret = blk_register_tracepoints();
  if (ret) return ret;
  return 0;
}

/**
 * exit the ntm blk layer
 * - remove the proc entries
 * - unregister the tracepoints
 * - clean the variables
 */
static void exit_blk_layer_monitor(void) {
  remove_blk_layer_proc_entries();
  blk_unregister_tracepoints();
  free_blk_layer_variables();
}

#endif  // _K_BLK_LAYER_H_