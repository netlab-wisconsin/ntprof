#ifndef _BLK_LAYER_H_
#define _BLK_LAYER_H_

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
#include <trace/events/nvme_tcp.h>

#include "ntm_kernel.h"



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

/** sliding window */
struct sliding_window {
  /** a lock free linked list of <timestamp, request> */
  struct list_head list;
  /** count */
  atomic64_t count;
  spinlock_t lock;
};

/**
 * initialize a sliding window
 * @param sw: the sliding window to be initialized
*/
void init_sliding_window(struct sliding_window *sw) {
  INIT_LIST_HEAD(&sw->list);
  atomic64_set(&sw->count, 0);
  spin_lock_init(&sw->lock);
}


/** an abstract of an io, a node in the sliding window */
struct bio_info {
  struct list_head list;
  u64 ts;
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
  info->ts = ktime_get_ns();
  info->size = bio->bi_iter.bi_size;
  info->type = bio_data_dir(bio);
  info->pos = bio->bi_iter.bi_sector;
}






void sw_all_to_blk_stat(struct sliding_window *sw, struct blk_stat *tr) {
  init_blk_tr(tr);
  struct bio_info *info;
  struct list_head *pos, *q;

  list_for_each_safe(pos, q, &sw->list) {
    info = list_entry(pos, struct bio_info, list);
    unsigned long long *cnt = info->type ? &tr->write_count : &tr->read_count;
    unsigned long long *io = info->type ? tr->write_io : tr->read_io;
    (*cnt)++;
    if (info->size < 4096) {
      io[_LT_4K]++;
    } else if (info->size == 4096) {
      io[_4K]++;
    } else if (info->size == 8192) {
      io[_8K]++;
    } else if (info->size == 16384) {
      io[_16K]++;
    } else if (info->size == 32768) {
      io[_32K]++;
    } else if (info->size == 65536) {
      io[_64K]++;
    } else if (info->size == 131072) {
      io[_128K]++;
    } else if (info->size > 131072) {
      io[_GT_128K]++;
    } else {
      io[_OTHERS]++;
    }
  }
}

void sw_to_blk_stat(struct sliding_window *sw, struct blk_stat *tr,
                           u64 expire) {
  init_blk_tr(tr);
  struct bio_info *info;
  struct list_head *pos, *q;

  // traverse the list in reverse order
  list_for_each_prev_safe(pos, q, &sw->list) {
    info = list_entry(pos, struct bio_info, list);
    if (info->ts < expire) {
      break;
    }
    unsigned long long *cnt = info->type ? &tr->write_count : &tr->read_count;
    unsigned long long *io = info->type ? tr->write_io : tr->read_io;
    (*cnt)++;
    if (info->size < 4096) {
      io[_LT_4K]++;
    } else if (info->size == 4096) {
      io[_4K]++;
    } else if (info->size == 8192) {
      io[_8K]++;
    } else if (info->size == 16384) {
      io[_16K]++;
    } else if (info->size == 32768) {
      io[_32K]++;
    } else if (info->size == 65536) {
      io[_64K]++;
    } else if (info->size == 131072) {
      io[_128K]++;
    } else if (info->size > 131072) {
      io[_GT_128K]++;
    } else {
      io[_OTHERS]++;
    }
  }
}

static void insert_sw(struct sliding_window *sw, struct bio_info *info) {
  /** add the info to the tail of linked list */
  spin_lock(&sw->lock);
  list_add_tail(&info->list, &sw->list);
  atomic64_inc(&sw->count);
  spin_unlock(&sw->lock);
}

static u64 sw_remove_old(struct sliding_window *sw, u64 expire_ts) {
  struct bio_info *info;
  struct list_head *pos, *q;
  u64 cnt = 0;
  spin_lock(&sw->lock);
  list_for_each_safe(pos, q, &sw->list) {
    info = list_entry(pos, struct bio_info, list);
    if (info->ts < expire_ts) {
      list_del(pos);
      atomic64_dec(&sw->count);
      kfree(info);
      cnt++;
    } else {
      break;
    }
  }
  spin_unlock(&sw->lock);
  return cnt;
}

#define SAMPLE_RATE 0.001

static struct _blk_stat *_raw_blk_stat;
static struct blk_stat *raw_blk_stat;
static struct sliding_window *sw;
static struct blk_stat *sample_10s;
static struct blk_stat *sample_2s;

static int record_enabled = 0;

/**
 * communication entries
 */
struct proc_dir_entry *parent_dir;
struct proc_dir_entry *entry_ctrl;
struct proc_dir_entry *entry_raw_blk_stat;
struct proc_dir_entry *entry_params;
struct proc_dir_entry *entry_sw;
struct proc_dir_entry *entry_sample_10s;
struct proc_dir_entry *entry_sample_2s;

static bool to_sample(void) {
  unsigned int rand;
  get_random_bytes(&rand, sizeof(rand));
  return rand < SAMPLE_RATE * UINT_MAX;
}

unsigned int get_pending_requests(struct request_queue *q) { return 0; }

void on_block_bio_queue(void *data, struct bio *bio) {
  if (record_enabled) {
    atomic64_t *arr = NULL;
    unsigned int size = bio->bi_iter.bi_size;
    const char *rq_disk_name = bio->bi_bdev->bd_disk->disk_name;

    if (strcmp(device_name, rq_disk_name) != 0) {
      return;
    }

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

    switch (size) {
      case 4096:
        atomic64_inc(&arr[_4K]);
        break;
      case 8192:
        atomic64_inc(&arr[_8K]);
        break;
      case 16384:
        atomic64_inc(&arr[_16K]);
        break;
      case 32768:
        atomic64_inc(&arr[_32K]);
        break;
      case 65536:
        atomic64_inc(&arr[_64K]);
        break;
      case 131072:
        atomic64_inc(&arr[_128K]);
        break;
      default:
        if (size < 4096) {
          atomic64_inc(&arr[_LT_4K]);
        } else if (size > 131072) {
          atomic64_inc(&arr[_GT_128K]);
        } else {
          atomic64_inc(&arr[_OTHERS]);
        }
    }

    if (to_sample()) {
      struct bio_info *info = kmalloc(sizeof(*info), GFP_KERNEL);
      if (!info) {
        pr_err("Failed to allocate memory for bio_info\n");
        return;
      }
      extract_bio_info(info, bio);
      insert_sw(sw, info);
    }
  }
}

static int blk_register_tracepoints(void) {
  int ret;
  ret = register_trace_block_bio_queue(on_block_bio_queue, NULL);
  if (ret) {
    pr_err("Failed to register tracepoint block_bio_queue\n");
    return ret;
  }
  return 0;
}

static void blk_unregister_tracepoints(void) {
  unregister_trace_block_bio_queue(on_block_bio_queue, NULL);
}

/**
 * update data periodically with the user
 */
static int update_routine_fn(void *data) {
  while (!kthread_should_stop()) {
    copy_blk_stat(raw_blk_stat, _raw_blk_stat);
    if (device_name[0] != '\0') {
      struct request_queue *q = device_name_to_queue(device_name);
      struct blk_mq_hw_ctx *hctx;
      unsigned int i;
      queue_for_each_hw_ctx(q, hctx, i) {
        pr_info("nr_io : %d\n", hctx->nr_active);
      }
    }
    u64 now = ktime_get_ns();
    sw_remove_old(sw, now - 10 * NSEC_PER_SEC);
    sw_all_to_blk_stat(sw, sample_10s);
    sw_to_blk_stat(sw, sample_2s, now - 2 * NSEC_PER_SEC);
    msleep(1000);  // Sleep for 1 second
  }
  pr_info("update_routine_thread exiting\n");
  return 0;
}

/**
 * ---------------------- ctrl command ----------------------
 */

static ssize_t proc_ctrl_read(struct file *file, char __user *buffer,
                              size_t count, loff_t *pos) {
  char buf[4];
  int len = snprintf(buf, sizeof(buf), "%d\n", record_enabled);
  if (*pos >= len) return 0;
  if (count < len) return -EINVAL;
  if (copy_to_user(buffer, buf, len)) return -EFAULT;
  *pos += len;
  return len;
}

static ssize_t proc_ctrl_write(struct file *file, const char __user *buffer,
                               size_t count, loff_t *pos) {
  char buf[4];
  if (count > sizeof(buf) - 1) return -EINVAL;
  if (copy_from_user(buf, buffer, count)) return -EFAULT;
  buf[count] = '\0';

  if (buf[0] == '1') {
    if (!record_enabled) {
      record_enabled = 1;
      update_routine_thread =
          kthread_run(update_routine_fn, NULL, "update_routine_thread");
      if (IS_ERR(update_routine_thread)) {
        pr_err("Failed to create copy thread\n");
        record_enabled = 0;
        return PTR_ERR(update_routine_thread);
      }
      pr_info("update_routine_thread created\n");
    }
  } else if (buf[0] == '0') {
    if (record_enabled) {
      record_enabled = 0;
      if (update_routine_thread) {
        kthread_stop(update_routine_thread);
        update_routine_thread = NULL;
      }
    }
  } else {
    return -EINVAL;
  }

  return count;
}

static const struct proc_ops ntm_ctrl_ops = {
    .proc_read = proc_ctrl_read,
    .proc_write = proc_ctrl_write,
};

/**
 * ---------------------- params command ----------------------
 */

static ssize_t ntm_params_write(struct file *file, const char __user *buffer,
                                size_t count, loff_t *pos) {
  char buf[32];
  if (count > sizeof(buf) - 1) return -EINVAL;
  if (copy_from_user(buf, buffer, count)) return -EFAULT;
  buf[count] = '\0';

  // if input is not empty, copy the whole input to the device_name
  if (count == 1) {
    return -EINVAL;
  } else {
    strncpy(device_name, buf, sizeof(device_name));
    pr_info("device_name set to %s\n", device_name);
  }
  return count;
}

static const struct proc_ops ntm_params_ops = {
    .proc_write = ntm_params_write,
};

/**
 * ---------------------- raw_blk_stat ----------------------
 */

static int mmap_raw_blk_stat(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(raw_blk_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_raw_blk_stat_ops = {
    .proc_mmap = mmap_raw_blk_stat,
};

/**
 * ---------------------- sample_10s ----------------------
 */
static int mmap_sample_10s(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(sample_10s),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_sample_10s_ops = {
    .proc_mmap = mmap_sample_10s,
};

/**
 * ---------------------- sample_2s ----------------------
 */

static int mmap_sample_2s(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(sample_2s),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_sample_2s_ops = {
    .proc_mmap = mmap_sample_2s,
};

/**
 * ---------------------- initializing module ----------------------
 */

int init_variables(void) {
  record_enabled = 0;
  device_name[0] = '\0';
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
  /** sample_10s */
  sample_10s = vmalloc(sizeof(*sample_10s));
  if (!sample_10s) return -ENOMEM;
  init_blk_tr(sample_10s);
  /** sample_2s */
  sample_2s = vmalloc(sizeof(*sample_2s));
  if (!sample_2s) return -ENOMEM;
  init_blk_tr(sample_2s);
  return 0;
}

int clean_variables(void) {
  vfree(_raw_blk_stat);
  vfree(raw_blk_stat);
  vfree(sw);
  vfree(sample_10s);
  vfree(sample_2s);
  return 0;
}

int init_proc_entries(void) {
  parent_dir = proc_mkdir("ntm", NULL);
  if (!parent_dir) {
    pr_err("Failed to create /proc/ntm directory\n");
    return -ENOMEM;
  }

  entry_ctrl = proc_create("ntm_ctrl", 0666, parent_dir, &ntm_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for control\n");
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_raw_blk_stat =
      proc_create("ntm_raw_blk_stat", 0666, parent_dir, &ntm_raw_blk_stat_ops);
  if (!entry_raw_blk_stat) {
    pr_err("Failed to create proc entry for data\n");
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_params = proc_create("ntm_params", 0666, parent_dir, &ntm_params_ops);
  if (!entry_params) {
    pr_err("Failed to create proc entry for params\n");
    remove_proc_entry("ntm_raw_blk_stat", parent_dir);
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_sample_10s =
      proc_create("ntm_sample_10s", 0666, parent_dir, &ntm_sample_10s_ops);
  if (!entry_sample_10s) {
    pr_err("Failed to create proc entry for sample_10s\n");
    remove_proc_entry("ntm_params", parent_dir);
    remove_proc_entry("ntm_raw_blk_stat", parent_dir);
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_sample_2s =
      proc_create("ntm_sample_2s", 0666, parent_dir, &ntm_sample_2s_ops);
  if (!entry_sample_2s) {
    pr_err("Failed to create proc entry for sample_2s\n");
    remove_proc_entry("ntm_sample_10s", parent_dir);
    remove_proc_entry("ntm_params", parent_dir);
    remove_proc_entry("ntm_raw_blk_stat", parent_dir);
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  return 0;
}

static void remove_proc_entries(void) {
  remove_proc_entry("ntm_raw_blk_stat", parent_dir);
  remove_proc_entry("ntm_ctrl", parent_dir);
  remove_proc_entry("ntm_params", parent_dir);
  remove_proc_entry("ntm_sample_10s", parent_dir);
  remove_proc_entry("ntm_sample_2s", parent_dir);
  remove_proc_entry("ntm", NULL);
}

static int _init_ntm_blk_layer(void) {
  int ret;
  ret = init_variables();
  if (ret) return ret;
  ret = init_proc_entries();
  if (ret) {
    clean_variables();
    return ret;
  }

  ret = blk_register_tracepoints();
  if (ret) {
    remove_proc_entries();
    clean_variables();
    return ret;
  }
  pr_info("ntm module loaded\n");
  return 0;
}

static void _exit_ntm_blk_layer(void) {
  remove_proc_entries();
  blk_unregister_tracepoints();
  clean_variables();
  pr_info("ntm module unloaded\n");
}

#endif // _BLK_LAYER_H_