#include "k_nttm.h"

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "k_nvmet_tcp_layer.h"

/**
 * update data periodically with the user
 */
static int update_routine_fn(void *data) {
  while (!kthread_should_stop()) {
    u64 now = ktime_get_ns();
    
    /** update the statistic here */
    nvmet_tcp_stat_update(now);

    /** wait for 1 second to start routine again */
    msleep(1000);
  }
  /** exit point */
  pr_info("update_routine_thread exiting\n");
  return 0;
}

/**
 * This function defines how user space read ctrl variable
 * @param file: the file to read
 * @param buffer: the buffer to store the content from the user space
 * @param count: the size of the buffer
 * @param pos: the position to read
 */
static ssize_t proc_ctrl_read(struct file *file, char __user *buffer,
                              size_t count, loff_t *pos) {
  /** load the content of ctrl to a tmp variable, buf*/
  char buf[4];
  int len = snprintf(buf, sizeof(buf), "%d\n", ctrl);
  if (*pos >= len) return 0;
  if (count < len) return -EINVAL;

  /** copy the content of the tmp variable, buf, to the user space file buffer
   */
  if (copy_to_user(buffer, buf, len)) return -EFAULT;
  *pos += len;
  return len;
}

/**
 * This function defines how user space write to ctrl variable
 * @param file: the file to write
 * @param buffer: the buffer to store the content, it should be 0 or 1
 * @param count: the size of the buffer
 * @param pos: the position to write
 */
static ssize_t proc_ctrl_write(struct file *file, const char __user *buffer,
                               size_t count, loff_t *pos) {
  /** initialize a temp buffer to store the msg from the user space */
  char buf[4];
  if (count > sizeof(buf) - 1) return -EINVAL;

  /** copy the msg from thr user space to buf */
  if (copy_from_user(buf, buffer, count)) return -EFAULT;

  /** set the end of the buf */
  buf[count] = '\0';

  /**
   * If the mesage is 1
   *  - enable the record
   *  - start the update_routine_thread
   * If the message is 0
   *  - disable the record
   *  - stop the update_routine_thread
   */
  if (buf[0] == '1') {
    if (!ctrl) {
      ctrl = 1;
      update_routine_thread =
          kthread_run(update_routine_fn, NULL, "update_routine_thread");
      if (IS_ERR(update_routine_thread)) {
        pr_err("Failed to create copy thread\n");
        ctrl = 0;
        return PTR_ERR(update_routine_thread);
      }
      pr_info("update_routine_thread created\n");
    }
  } else if (buf[0] == '0') {
    if (ctrl) {
      ctrl = 0;
      if (update_routine_thread) {
        kthread_stop(update_routine_thread);
        update_routine_thread = NULL;
      }
    }
  } else {
    /** unknown msg from user space */
    return -EINVAL;
  }

  return count;
}

/**
 * define the operations for the control command
 * - proc_read: read the ctrl variable
 * - proc_write: write the ctrl variable
 */
static const struct proc_ops nttm_ctrl_ops = {
    .proc_read = proc_ctrl_read,
    .proc_write = proc_ctrl_write,
};

static int mmap_nttm_params(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(args),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops nttm_params_ops = {
    .proc_mmap = mmap_nttm_params,
};
/**
 * initialize the proc entries
 * - /proc/nttm
 * - /proc/nttm/ctrl
 * - /proc/nttm/args
 */
int init_nttm_proc_entries(void) {
  /**   * create /proc/nttm directory   */
  parent_dir = proc_mkdir("nttm", NULL);
  if (!parent_dir) {
    pr_err("Failed to create /proc/nttm directory\n");
    goto failed;
  }
  /**   * create /proc/nttm/ctrl   */
  entry_ctrl = proc_create("ctrl", 0666, parent_dir, &nttm_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for ctrl\n");
    goto cleanup_nttm_dir;
  }

  /** create /proc/nttm/args */
  entry_args = proc_create("args", 0, parent_dir, &nttm_params_ops);
  if (!entry_args) {
    pr_err("Failed to create proc entry for args\n");
    goto cleanup_ctrl;
  }
  return 0;

cleanup_ctrl:
  pr_info("clean up ctrl\n");
  remove_proc_entry("nttm_ctrl", parent_dir);
cleanup_nttm_dir:
  pr_info("clean up nttm\n");
  remove_proc_entry("nttm", NULL);
failed:
  return -ENOMEM;
}

void remove_proc_entries(void) {
  remove_proc_entry("args", parent_dir);
  remove_proc_entry("ctrl", parent_dir);
  remove_proc_entry("nttm", NULL);
}

void init_global_variables(void) {
  ctrl = 0;
  args = vmalloc(sizeof(Arguments));
}

void free_global_variables(void) { vfree(args); }

static int __init init_nttm_module(void) {
  int ret;
  pr_info("NVMeTCP monitoring module initialized\n");
  init_global_variables();
  ret = init_nttm_proc_entries();
  if(ret) return ret;

  /** initialize the monitor modules on different layers */
  init_nvmet_tcp_layer();

  return 0;
}

static void __exit exit_nttm_module(void) {
  pr_info("NVMeTCP monitoring module exited\n");

  /** exit monitor modules on different layers */
  exit_nvmet_tcp_layer();

  remove_proc_entries();
  free_global_variables();
}

module_init(init_nttm_module);
module_exit(exit_nttm_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
