#include "nvmetcp_monitor_kernel.h"

#include <linux/blkdev.h>
#include <linux/delay.h>
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
#include <linux/device.h>
#include <linux/blk-mq.h>


#define BUFFER_SIZE PAGE_SIZE

static struct _blk_tr *_tr_data;
static struct blk_tr *tr_data;

static int record_enabled = 0;

/** for storing the device name */
static char device_name[32] = "";

static struct task_struct *monitor_copy_thread;

/** sliding window on time */
struct sw {
  /** a lock free linked list of <timestamp, request> */
  struct list_head list;
  /** count */
  atomic64_t count;
};

// 获取 device 对应的 request queue
struct request_queue *get_request_queue_by_name(const char *dev_name)
{
    struct block_device *bdev;
    struct request_queue *q = NULL;
    struct inode *inode;
    int error;

    // 获取 block device 结构
    // add /dev/ prefix
    char full_name[32];
    snprintf(full_name, sizeof(full_name), "/dev/%s", dev_name);

    bdev = blkdev_get_by_path(full_name, FMODE_READ, NULL);
    if (IS_ERR(bdev)) {
        pr_err("Failed to get block device for device %s\n", full_name);
        return NULL;
    }

    // 获取 request queue
    q = bdev_get_queue(bdev);
    if (!q) {
        pr_err("Failed to get request queue for device %s\n", full_name);
    }

    // 释放 block device
    blkdev_put(bdev, FMODE_READ);

    return q;
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

void nvmetcp_monitor_trace_func(void *data, struct bio *bio) {
  if (record_enabled) {
    atomic64_t *arr = NULL;
    unsigned int size = bio->bi_iter.bi_size;
    const char *rq_disk_name = bio->bi_bdev->bd_disk->disk_name;

    if (strcmp(device_name, rq_disk_name) != 0) {
      return;
    }

    if (bio_data_dir(bio) == READ) {
      arr = _tr_data->read_io;
      atomic64_inc(&_tr_data->read_count);
    } else {
      arr = _tr_data->write_io;
      atomic64_inc(&_tr_data->write_count);
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

    // unsigned long long v = get_pending_requests(rq->q);
    // unsigned long long v = atomic64_read(&rq->q->pending_rq);
    // pr_info("pending requests: %llu\n", v);
  }
}

void blk_add_trace_rq_insert_(void *ignore, struct request *rq){
  return;
}

static void blk_register_tracepoints(void)
{
    int ret;
    ret = register_trace_block_bio_queue(nvmetcp_monitor_trace_func, NULL);
    WARN_ON(ret);
    // ret = register_trace_block_rq_insert(blk_add_trace_rq_insert_, NULL);
    // WARN_ON(ret);
}

static void blk_unregister_tracepoints(void)
{
    unregister_trace_block_bio_queue(nvmetcp_monitor_trace_func, NULL);
    // unregister_trace_block_rq_insert(blk_add_trace_rq_insert_, NULL);
}

static int monitor_copy_thread_fn(void *data) {
  while (!kthread_should_stop()) {
    copy_blk_tr(tr_data, _tr_data);
    if (device_name[0] != '\0') {
      // get the request queue from the device name
      struct request_queue *q = get_request_queue_by_name(device_name);
      if (q) {
        tr_data->pending_rq = get_pending_requests(q);
      }
    }
    msleep(1000);  // Sleep for 1 second
  }
  pr_info("monitor_copy_thread_fn exiting\n");
  return 0;
}

static ssize_t nvmetcp_monitor_ctrl_read(struct file *file, char __user *buffer,
                                         size_t count, loff_t *pos) {
  char buf[4];
  int len = snprintf(buf, sizeof(buf), "%d\n", record_enabled);
  if (*pos >= len) return 0;
  if (count < len) return -EINVAL;
  if (copy_to_user(buffer, buf, len)) return -EFAULT;
  *pos += len;
  return len;
}

static ssize_t nvmetcp_monitor_ctrl_write(struct file *file,
                                          const char __user *buffer,
                                          size_t count, loff_t *pos) {
  char buf[4];
  if (count > sizeof(buf) - 1) return -EINVAL;
  if (copy_from_user(buf, buffer, count)) return -EFAULT;
  buf[count] = '\0';

  if (buf[0] == '1') {
    if (!record_enabled) {
      record_enabled = 1;
      monitor_copy_thread =
          kthread_run(monitor_copy_thread_fn, NULL, "monitor_copy_thread");
      if (IS_ERR(monitor_copy_thread)) {
        pr_err("Failed to create copy thread\n");
        record_enabled = 0;
        return PTR_ERR(monitor_copy_thread);
      }
      pr_info("monitor_copy_thread created\n");
    }
  } else if (buf[0] == '0') {
    if (record_enabled) {
      record_enabled = 0;
      if (monitor_copy_thread) {
        kthread_stop(monitor_copy_thread);
        monitor_copy_thread = NULL;
      }
    }
  } else {
    return -EINVAL;
  }

  return count;
}

static int nvmetcp_monitor_mmap(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(tr_data),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

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

static const struct proc_ops nvmetcp_monitor_ctrl_ops = {
    .proc_read = nvmetcp_monitor_ctrl_read,
    .proc_write = nvmetcp_monitor_ctrl_write,
    .proc_mmap = nvmetcp_monitor_mmap,
};

static const struct proc_ops nvmetcp_monitor_data_ops = {
    .proc_mmap = nvmetcp_monitor_mmap,
};

static const struct proc_ops nvmetcp_monitor_params_ops = {
    .proc_write = nvmetcp_monitor_params_write,
};

static int __init nvmetcp_monitor_init(void) {
  int ret;
  struct proc_dir_entry *entry_ctrl;
  struct proc_dir_entry *entry_data;
  struct proc_dir_entry *entry_params;

  _tr_data = vmalloc(sizeof(*_tr_data));
  if (!_tr_data) return -ENOMEM;
  _init_blk_tr(_tr_data);

  tr_data = vmalloc(sizeof(*tr_data));
  if (!tr_data) return -ENOMEM;

  init_blk_tr(tr_data);

  blk_register_tracepoints();

  entry_ctrl = proc_create("nvmetcp_monitor_ctrl", 0666, NULL,
                           &nvmetcp_monitor_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for control\n");
    blk_unregister_tracepoints();
    vfree(tr_data);
    return -ENOMEM;
  }

  entry_data = proc_create("nvmetcp_monitor_data", 0666, NULL,
                           &nvmetcp_monitor_data_ops);
  if (!entry_data) {
    pr_err("Failed to create proc entry for data\n");
    remove_proc_entry("nvmetcp_monitor_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(tr_data);
    return -ENOMEM;
  }

  entry_params = proc_create("nvmetcp_monitor_params", 0666, NULL,
                             &nvmetcp_monitor_params_ops);
  if (!entry_params) {
    pr_err("Failed to create proc entry for params\n");
    remove_proc_entry("nvmetcp_monitor_data", NULL);
    remove_proc_entry("nvmetcp_monitor_ctrl", NULL);
    blk_unregister_tracepoints();
    vfree(tr_data);
    return -ENOMEM;
  }

  pr_info("nvmetcp_monitor module loaded\n");
  return 0;
}

static void __exit nvmetcp_monitor_exit(void) {
  if (monitor_copy_thread) {
    kthread_stop(monitor_copy_thread);
  }

  remove_proc_entry("nvmetcp_monitor_data", NULL);
  remove_proc_entry("nvmetcp_monitor_ctrl", NULL);
  remove_proc_entry("nvmetcp_monitor_params", NULL);
  blk_unregister_tracepoints();
  vfree(tr_data);
  vfree(_tr_data);
  pr_info("nvmetcp_monitor module unloaded\n");
}

module_init(nvmetcp_monitor_init);
module_exit(nvmetcp_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
