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
   * refers to enum SIZE_TYPE in ntm_com.h
   */
  atomic64_t read_io[9];
  /** write io number of different sizes */
  atomic64_t write_io[9];

  /** TODO: number of io in-flight */
  atomic64_t pending_rq;
};

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
 */
void inc_cnt_atomic_arr(atomic64_t *arr, int size) {
  if (size < 4096) {
    atomic64_inc(&arr[_LT_4K]);
  } else if (size == 4096) {
    atomic64_inc(&arr[_4K]);
  } else if (size == 8192) {
    atomic64_inc(&arr[_8K]);
  } else if (size == 16384) {
    atomic64_inc(&arr[_16K]);
  } else if (size == 32768) {
    atomic64_inc(&arr[_32K]);
  } else if (size == 65536) {
    atomic64_inc(&arr[_64K]);
  } else if (size == 131072) {
    atomic64_inc(&arr[_128K]);
  } else {
    if (size > 131072) {
      atomic64_inc(&arr[_GT_128K]);
    } else {
      atomic64_inc(&arr[_OTHERS]);
    }
  }
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
  for (i = 0; i < 9; i++) {
    atomic64_set(&tr->read_io[i], 0);
    atomic64_set(&tr->write_io[i], 0);
  }
  atomic64_set(&tr->pending_rq, 0);
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
  }
}

/** an abstract of an io, a node in the sliding window */
struct bio_info {
  struct list_head list;
  u64 size;
  u64 pos;
  bool type;
};

/**
 * extract bio infor from a bio
 * @param info: the bio_info to store the extracted information
 * @param bio: the bio to extract information from
 */
void extract_bio_info(struct bio_info *info, struct bio *bio) {
  info->size = bio->bi_iter.bi_size;
  info->type = bio_data_dir(bio);
  info->pos = bio->bi_iter.bi_sector;
}

/**
 * make the statistic data for the whole sliding window
 * the sliding window will be cleared first
 * @param sw: the sliding window
 * @param tr: the blk_stat to store the statistic data
 */
void sw_all_to_blk_stat(struct sliding_window *sw, struct blk_stat *tr) {
  struct bio_info *info;
  struct list_head *pos, *q;

  init_blk_tr(tr);
  list_for_each_safe(pos, q, &sw->list) {
    unsigned long long *cnt, *io;
    info = (list_entry(pos, struct sw_node, list))->data;
    if (info->type) {
      cnt = &tr->write_count;
      io = tr->write_io;
    } else {
      cnt = &tr->read_count;
      io = tr->read_io;
    }
    (*cnt)++;
    inc_cnt_arr(io, info->size);
  }
}

/**
 * Make the statistic data for the sliding window in a specific time range
 * The io before the expire time will be ignored
 * Assuming the sliding window is sorted in ascending order, i.e., the latest io
 * is at the tail of the list.
 *
 * @param sw: the sliding window
 * @param tr: the blk_stat to store the statistic data
 * @param expire: the expire time
 */
void sw_to_blk_stat(struct sliding_window *sw, struct blk_stat *tr,
                    u64 expire) {
  struct sw_node *node;
  struct bio_info *info;
  struct list_head *pos, *q;

  init_blk_tr(tr);

  // traverse the list in reverse order
  list_for_each_prev_safe(pos, q, &sw->list) {
    unsigned long long *cnt, *io;
    node = list_entry(pos, struct sw_node, list);
    info = node->data;
    if (node->timestamp < expire) {
      break;
    }
    if (info->type) {
      cnt = &tr->write_count;
      io = tr->write_io;
    } else {
      cnt = &tr->read_count;
      io = tr->read_io;
    }
    (*cnt)++;
    inc_cnt_arr(io, info->size);
  }
}



/** in-kernel strucutre to update blk layer statistics */
static struct _blk_stat *_raw_blk_stat;

/** raw blk layer statistics, shared in user space*/
static struct blk_stat *raw_blk_stat;

/** a sliding window to record bio in the last period */
static struct sliding_window *sw;

/** blk layer stat for sampled io, in last period, shared with user space */
static struct blk_stat *sample;

/**
 * communication entries
 */
struct proc_dir_entry *entry_blk_dir;
struct proc_dir_entry *entry_raw_blk_stat;
struct proc_dir_entry *entry_params;
struct proc_dir_entry *entry_sw;
struct proc_dir_entry *entry_sample;

/**
 * TODO: get the number of pending requests in the queue
 */
unsigned int get_pending_requests(struct request_queue *q) { return 0; }

/**
 * This function is triggered when a bio is queued. This is the main function to
 * trace the io in the blk layer.
 * @param ignore:
 * @param bio: the bio to be traced
 */
void on_block_bio_queue(void *ignore, struct bio *bio) {
  /** only take action when the record is enabled */
  if (record_enabled) {
    atomic64_t *arr = NULL;
    unsigned int size;

    /** read the disk name */
    const char *rq_disk_name = bio->bi_bdev->bd_disk->disk_name;

    /** filter out the io from other devices */
    if (strcmp(args.dev, rq_disk_name) != 0) {
      return;
    }

    /** read the size of the io */
    size = bio->bi_iter.bi_size;

    /** read the io direction and increase the corresponding counter */
    if (bio_data_dir(bio) == READ) {
      arr = _raw_blk_stat->read_io;
      atomic64_inc(&_raw_blk_stat->read_count);
    } else {
      arr = _raw_blk_stat->write_io;
      atomic64_inc(&_raw_blk_stat->write_count);
    }
    if (!arr) {
      pr_info("io is neither read nor write.");
      return;
    }

    /** increase the counter of the corresponding size category */
    inc_cnt_atomic_arr(arr, size);

    /** if sampled, insert to the sliding window */
    if (to_sample()) {
      struct bio_info *info;
      struct sw_node *node;
      
      info = kmalloc(sizeof(*info), GFP_KERNEL);
      if (!info) {
        pr_err("Failed to allocate memory for bio_info\n");
        return;
      }
      extract_bio_info(info, bio);
      // use general function instead
      node = kmalloc(sizeof(*node), GFP_KERNEL);
      node->timestamp = ktime_get_ns();
      node->data = info;
      add_to_sliding_window(sw, node);
    }
  }
}

/**
 * register the tracepoints
 * - block_bio_queue
 */
static int blk_register_tracepoints(void) {
  int ret;
  ret = register_trace_block_bio_queue(on_block_bio_queue, NULL);
  if (ret) {
    pr_err("Failed to register tracepoint block_bio_queue\n");
    return ret;
  }
  return 0;
}

/**
 * unregister the tracepoints
 * - block_bio_queue
 */
static void blk_unregister_tracepoints(void) {
  unregister_trace_block_bio_queue(on_block_bio_queue, NULL);
}

/**
 * This function defines how user space map the a variable to
 * the kernel space variable, raw_blk_stat
 * @param filp: the file to map
 * @param vma: the virtual memory area to map
 * @return 0 if success, -EAGAIN if failed
 */
static int mmap_raw_blk_stat(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(raw_blk_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

/**
 * define the operations for the raw_blk_stat command
 * - proc_mmap: map the raw_blk_stat to the user space
 */
static const struct proc_ops ntm_raw_blk_stat_ops = {
    .proc_mmap = mmap_raw_blk_stat,
};

/**
 * This function defines how user space map the a variable to
 * the kernel space variable, sample
 * @param filp: the file to map
 * @param vma: the virtual memory area to map
 * @return 0 if success, -EAGAIN if failed
 */
static int mmap_sample(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(sample),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

/**
 * define the operations for the sample command
 * - proc_mmap: map the sample to the user space
 */
static const struct proc_ops ntm_sample_ops = {
    .proc_mmap = mmap_sample,
};

/**
 * update the blk layer statistic periodically
 * This function will be triggered in the main routine thread
 */
void blk_stat_update(u64 now) {
  /** update the raw blk layer statistic in the user space */
  copy_blk_stat(raw_blk_stat, _raw_blk_stat);
  /** remove expired io in the sliding window */
  remove_from_sliding_window(sw, now - args.win * NSEC_PER_SEC);
  /** update the stat of sampled io in last period, shared in the user space */
  sw_all_to_blk_stat(sw, sample);
}

/**
 * initialize the variables
 * - set record_enabled to 0
 * - set device_name to empty string
 * - allocate memory for _raw_blk_stat, raw_blk_stat, sw, sample
 * - initialize the blk_stat structures
 */
int init_blk_variables(void) {
  /** _raw_blk_stat */
  _raw_blk_stat = vmalloc(sizeof(*_raw_blk_stat));
  if (!_raw_blk_stat) return -ENOMEM;
  _init_blk_tr(_raw_blk_stat);
  /** raw_blk_stat */
  raw_blk_stat = vmalloc(sizeof(*raw_blk_stat));
  if (!raw_blk_stat) return -ENOMEM;
  init_blk_tr(raw_blk_stat);
  /** sw */
  sw = vmalloc(sizeof(*sw));
  if (!sw) return -ENOMEM;
  init_sliding_window(sw);
  /** sample */
  sample = vmalloc(sizeof(*sample));
  if (!sample) return -ENOMEM;
  init_blk_tr(sample);
  return 0;
}

/**
 * clean the variables
 * - free the memory of _raw_blk_stat, raw_blk_stat, sw, sample
 */
int clean_blk_variables(void) {
  vfree(_raw_blk_stat);
  vfree(raw_blk_stat);
  vfree(sw);
  vfree(sample);
  return 0;
}

/**
 * initialize the proc entries
 * - create /proc/ntm/blk dir
 * - create /proc/ntm/blk/ntm_raw_blk_stat entry
 * - create /proc/ntm/blk/ntm_sample entry
 */
int init_blk_proc_entries(void) {

  entry_blk_dir = proc_mkdir("blk", parent_dir);
  if (!entry_blk_dir) {
    pr_err("Failed to create proc blk\n");
    return -ENOMEM;
  }

  entry_raw_blk_stat =
      proc_create("ntm_raw_blk_stat", 0666, entry_blk_dir, &ntm_raw_blk_stat_ops);
  if (!entry_raw_blk_stat) {
    pr_err("Failed to create proc entry for data\n");
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_sample =
      proc_create("ntm_sample", 0666, entry_blk_dir, &ntm_sample_ops);
  if (!entry_sample) {
    pr_err("Failed to create proc entry for sample\n");
    remove_proc_entry("ntm_raw_blk_stat", entry_blk_dir);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  return 0;
}

/**
 * remove the proc entries
 * - remove /proc/ntm/ntm_raw_blk_stat entry
 * - remove /proc/ntm/ntm_sample entry
 * - remove /proc/ntm/blk dir
 */
static void remove_blk_proc_entries(void) {
  remove_proc_entry("ntm_raw_blk_stat", entry_blk_dir);
  remove_proc_entry("ntm_sample", entry_blk_dir);
  remove_proc_entry("blk", parent_dir);
}

/**
 * initialize the ntm blk layer
 * - initialize the variables
 * - initialize the proc entries
 * - register the tracepoints
 */
static int _init_ntm_blk_layer(void) {
  int ret;
  ret = init_blk_variables();
  if (ret) return ret;
  ret = init_blk_proc_entries();
  if (ret) {
    clean_blk_variables();
    return ret;
  }

  ret = blk_register_tracepoints();
  if (ret) {
    remove_blk_proc_entries();
    clean_blk_variables();
    return ret;
  }
  pr_info("ntm module loaded\n");
  return 0;
}

/**
 * exit the ntm blk layer
 * - remove the proc entries
 * - unregister the tracepoints
 * - clean the variables
 */
static void _exit_ntm_blk_layer(void) {
  remove_blk_proc_entries();
  blk_unregister_tracepoints();
  clean_blk_variables();
  pr_info("stop blk module monitor\n");
}

#endif  // _K_BLK_LAYER_H_