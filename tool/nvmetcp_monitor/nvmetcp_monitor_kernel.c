#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/tracepoint.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <trace/events/block.h>
#include <linux/random.h>

#include "nvmetcp_monitor_kernel.h"

#define SAMPLE_RATE 0.001

static struct _blk_stat *_raw_blk_stat;
static struct blk_stat *raw_blk_stat;
static struct sliding_window *sw;
static struct blk_stat *sample_10s;
static struct blk_stat *sample_2s;

static int record_enabled = 0;

/** for storing the device name */
static char device_name[32] = "";

static struct task_struct *update_routine_thread;

/**
 * communication entries
 */
struct proc_dir_entry *entry_ctrl;
struct proc_dir_entry *entry_raw_blk_stat;
struct proc_dir_entry *entry_params;
struct proc_dir_entry *entry_sw;
struct proc_dir_entry *entry_sample_10s;
struct proc_dir_entry *entry_sample_2s;

struct request_queue *device_name_to_queue(const char *dev_name) {
  struct block_device *bdev;
  struct request_queue *q = NULL;
  struct inode *inode;
  int error;

  char full_name[32];
  snprintf(full_name, sizeof(full_name), "/dev/%s", dev_name);

  bdev = blkdev_get_by_path(full_name, FMODE_READ, NULL);
  if (IS_ERR(bdev)) {
    pr_err("Failed to get block device for device %s\n", full_name);
    return NULL;
  }

  q = bdev_get_queue(bdev);
  if (!q) {
    pr_err("Failed to get request queue for device %s\n", full_name);
  }

  blkdev_put(bdev, FMODE_READ);
  return q;
}

static bool to_sample(void){
    unsigned int rand;
    get_random_bytes(&rand, sizeof(rand));
    return rand < SAMPLE_RATE * UINT_MAX;
}

unsigned int get_pending_requests(struct request_queue *q) {
  // struct blk_mq_hw_ctx *hctx;
  // unsigned long i;
  // unsigned int pending_requests = 0;

  // queue_for_each_hw_ctx(q, hctx, i) {
  //   spin_lock(&hctx->lock);
  //   pending_requests += hctx->dispatch_busy;
  //   spin_unlock(&hctx->lock);
  // }

  // return pending_requests;
  // struct block_device * bdev = q->disk->part0;
  // if (queue_is_mq(q))
  // 	return blk_mq_in_flight(q, q->disk->part0);

  return 0;
}

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

    if(to_sample()){
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
    // if (device_name[0] != '\0') {
    //   struct request_queue *q = device_name_to_queue(device_name);
    //   if (q) {
    //     tr_data->pending_rq = get_pending_requests(q);
    //   }
    // }
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

static const struct proc_ops nvmetcp_monitor_ctrl_ops = {
    .proc_read = proc_ctrl_read,
    .proc_write = proc_ctrl_write,
};

/**
 * ---------------------- params command ----------------------
 */

static ssize_t nvmetcp_monitor_params_write(struct file *file,
                                            const char __user *buffer,
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

static const struct proc_ops nvmetcp_monitor_params_ops = {
    .proc_write = nvmetcp_monitor_params_write,
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

static const struct proc_ops nvmetcp_monitor_raw_blk_stat_ops = {
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

static const struct proc_ops nvmetcp_monitor_sample_10s_ops = {
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

static const struct proc_ops nvmetcp_monitor_sample_2s_ops = {
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

int clean_variables(void){
  vfree(_raw_blk_stat);
  vfree(raw_blk_stat);
  vfree(sw);
  vfree(sample_10s);
  vfree(sample_2s);
  return 0;
}

int init_proc_entries(void) {
  entry_ctrl = proc_create("ntm_ctrl", 0666, NULL, &nvmetcp_monitor_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for control\n");
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_raw_blk_stat = proc_create("ntm_raw_blk_stat", 0666, NULL,
                                   &nvmetcp_monitor_raw_blk_stat_ops);
  if (!entry_raw_blk_stat) {
    pr_err("Failed to create proc entry for data\n");
    remove_proc_entry("ntm_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_params =
      proc_create("ntm_params", 0666, NULL, &nvmetcp_monitor_params_ops);
  if (!entry_params) {
    pr_err("Failed to create proc entry for params\n");
    remove_proc_entry("ntm_raw_blk_stat", NULL);
    remove_proc_entry("ntm_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_sample_10s = proc_create("ntm_sample_10s", 0666, NULL, &nvmetcp_monitor_sample_10s_ops);
  if (!entry_sample_10s) {
    pr_err("Failed to create proc entry for sample_10s\n");
    remove_proc_entry("ntm_params", NULL);
    remove_proc_entry("ntm_raw_blk_stat", NULL);
    remove_proc_entry("ntm_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  entry_sample_2s = proc_create("ntm_sample_2s", 0666, NULL, &nvmetcp_monitor_sample_2s_ops);
  if (!entry_sample_2s) {
    pr_err("Failed to create proc entry for sample_2s\n");
    remove_proc_entry("ntm_sample_10s", NULL);
    remove_proc_entry("ntm_params", NULL);
    remove_proc_entry("ntm_raw_blk_stat", NULL);
    remove_proc_entry("ntm_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }

  return 0;
}

static void remove_proc_entries(void) {
    remove_proc_entry("ntm_raw_blk_stat", NULL);
    remove_proc_entry("ntm_ctrl", NULL);
    remove_proc_entry("ntm_params", NULL);
    remove_proc_entry("ntm_sample_10s", NULL);
    remove_proc_entry("ntm_sample_2s", NULL);
}

static int __init init_ntm_module(void) {
  int ret;
  ret = init_variables();
  if (ret) 
    return ret;
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
  pr_info("nvmetcp_monitor module loaded\n");
  return 0;
}

static void __exit exit_ntm_module(void) {
  if (update_routine_thread) {
    kthread_stop(update_routine_thread);
  }

  remove_proc_entries();
  blk_unregister_tracepoints();
  clean_variables();
  pr_info("nvmetcp_monitor module unloaded\n");
}

module_init(init_ntm_module);
module_exit(exit_ntm_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
