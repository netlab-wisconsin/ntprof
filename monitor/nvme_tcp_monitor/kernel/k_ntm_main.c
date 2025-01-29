#include "k_blk_layer.h"
#include "k_ntm.h"
#include "k_nvme_tcp_layer.h"
#include "k_tcp_layer.h"

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

/**
 * Definition of some external variables, decleared in k_ntm.h
 */
Arguments *args;
struct proc_dir_entry *parent_dir;
int ctrl = 0;
int qid2port[MAX_QID];

struct proc_dir_entry *entry_ctrl;
struct proc_dir_entry *entry_args;
/** a thread, periodically update the communication data strucure */
static struct task_struct *update_routine_thread;

/** direct and file names */
static char *dir_name = "ntm";
static char *ctrl_name = "ctrl";
static char *args_name = "args";

/**
 * update the shared data periodically
 */
static int update_routine_fn(void *data) {
  while (!kthread_should_stop()) {
    u64 now = ktime_get_ns();
    blk_layer_update(now);
    nvme_tcp_stat_update(now);
    // tcp_stat_update();

    /** wait for 1 second to start routine again */
    msleep(1000);
  }
  return 0;
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
 * - proc_write: write the ctrl variable
 */
static const struct proc_ops ntm_ctrl_ops = {
    .proc_write = proc_ctrl_write,
};

static int mmap_ntm_params(struct file *filp, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(args),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops ntm_params_ops = {
    .proc_mmap = mmap_ntm_params,
};

int init_ntm_proc_entries(void) {
  /**  create /proc/ntm directory   */
  parent_dir = proc_mkdir(dir_name, NULL);
  if (!parent_dir) {
    pr_err("Failed to create /proc/%s directory\n", dir_name);
    goto failed;
  }
  /**  create /proc/ntm/ctrl   */
  entry_ctrl = proc_create(ctrl_name, 0666, parent_dir, &ntm_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for /proc/%s/%s\n", dir_name,
           ctrl_name);
    goto cleanup_ntm_dir;
  }
  /** create /proc/ntm/args */
  entry_args = proc_create(args_name, 0, parent_dir, &ntm_params_ops);
  if (!entry_args) {
    pr_err("Failed to create proc entry for /proc/%s/%s\n", dir_name,
           args_name);
    goto cleanup_ctrl;
  }
  return 0;

cleanup_ctrl:
  pr_info("clean up /proc/%s/%s\n", dir_name, ctrl_name);
  remove_proc_entry(ctrl_name, parent_dir);
cleanup_ntm_dir:
  pr_info("clean up /proc/%s\n", dir_name);
  remove_proc_entry(dir_name, NULL);
failed:
  return -ENOMEM;
}

void remove_proc_entries(void) {
  remove_proc_entry(args_name, parent_dir);
  remove_proc_entry(ctrl_name, parent_dir);
  remove_proc_entry(dir_name, NULL);
}

void init_global_variables(void) {
  int i;
  for (i = 0; i < MAX_QID; i++) qid2port[i] = -1;
  ctrl = 0;
  args = vmalloc(sizeof(Arguments));
}

void free_global_variables(void) { vfree(args); }

static int __init init_ntm_module(void) {
  int ret;

  init_global_variables();

  /** initialize the proc entries */
  ret = init_ntm_proc_entries();
  if (ret) return ret;

  /** setup the blk layer monitor */
  ret = init_blk_layer_monitor();
  if (ret) {
    pr_err("Failed to initialize ntm_blk_layer\n");
    exit_blk_layer_monitor();
    return ret;
  }

  ret = init_nvme_tcp_layer_monitor();
  if (ret) {
    pr_err("Failed to initialize ntm_nvmetcp_layer\n");
    exit_blk_layer_monitor();
    remove_proc_entries();
    return ret;
  }

  // ret = init_tcp_layer();
  // if (ret) {
  //   pr_err("Failed to initialize tcp layer\n");
  //   exit_blk_layer_monitor();
  //   exit_nvme_tcp_layer_monitor();
  //   remove_proc_entries();
  //   return ret;
  // }
  return 0;
}

static void __exit exit_ntm_module(void) {
  if (update_routine_thread) {
    kthread_stop(update_routine_thread);
  }

  /** exit the blk layer monitor */
  // exit_tcp_layer();
  exit_blk_layer_monitor();
  exit_nvme_tcp_layer_monitor();

  remove_proc_entries();
  free_global_variables();
}

module_init(init_ntm_module);
module_exit(exit_ntm_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
