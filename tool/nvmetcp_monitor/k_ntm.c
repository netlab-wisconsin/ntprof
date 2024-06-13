#include "k_blk_layer.h"
#include "k_nvmetcp_layer.h"

/**
 * update data periodically with the user
 */
static int update_routine_fn(void *data) {
  while (!kthread_should_stop()) {
    u64 now = ktime_get_ns();
    blk_stat_update(now);
    nvmetcp_stat_update(now);

    /** TODO: get the number of io in-flight in the queue*/
    // if (device_name[0] != '\0') {
    //   struct request_queue *q = device_name_to_queue(device_name);
    //   struct blk_mq_hw_ctx *hctx;
    //   unsigned int i;
    //   queue_for_each_hw_ctx(q, hctx, i) {
    //     pr_info("nr_io : %d\n", hctx->nr_active);
    //   }
    // }

    /** wait for 1 second to start routine again */
    msleep(1000);
  }
  /** exit point */
  pr_info("update_routine_thread exiting\n");
  return 0;
}


/**
 * This function defines how user space read record_enabled variable
 * @param file: the file to read
 * @param buffer: the buffer to store the content from the user space
 * @param count: the size of the buffer
 * @param pos: the position to read
 */
static ssize_t proc_ctrl_read(struct file *file, char __user *buffer,
                              size_t count, loff_t *pos) {
  /** load the content of record_enabled to a tmp variable, buf*/
  char buf[4];
  int len = snprintf(buf, sizeof(buf), "%d\n", record_enabled);
  if (*pos >= len) return 0;
  if (count < len) return -EINVAL;

  /** copy the content of the tmp variable, buf, to the user space file buffer
   */
  if (copy_to_user(buffer, buf, len)) return -EFAULT;
  *pos += len;
  return len;
}

/**
 * This function defines how user space write to record_enabled variable
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
    /** unknown msg from user space */
    return -EINVAL;
  }

  return count;
}



/**
 * define the operations for the control command
 * - proc_read: read the record_enabled variable
 * - proc_write: write the record_enabled variable
 */
static const struct proc_ops ntm_ctrl_ops = {
    .proc_read = proc_ctrl_read,
    .proc_write = proc_ctrl_write,
};

/**
 * This function defines how user space write to device_name variable
 * @param file: the file to write
 * @param buffer: the buffer to store the content in the user space, it should
 * be the device name
 * @param count: the size of the buffer
 * @param pos: the position to write
 */
static ssize_t ntm_params_write(struct file *file, const char __user *buffer,
                                size_t count, loff_t *pos) {
  /** initialize a tmp buffer to store the msg from user space */
  char buf[32];
  if (count > sizeof(buf) - 1) return -EINVAL;
  /** copy the buffer content (device name) from the user space*/
  if (copy_from_user(buf, buffer, count)) return -EFAULT;
  /** set the end of the string */
  buf[count] = '\0';

  /** if input is not empty, copy the whole input to the device_name */
  if (count == 1) {
    return -EINVAL;
  } else {
    /** set the device_name variable */
    strncpy(device_name, buf, sizeof(device_name));
    pr_info("device_name set to %s\n", device_name);
  }
  return count;
}

/**
 * define the operations for the params command,
 * user only need to write the device name, no need to read
 * - proc_write: write the device name to the device_name variable
 */
static const struct proc_ops ntm_params_ops = {
    .proc_write = ntm_params_write,
};



/**
 * initialize the proc entries
 * - /proc/ntm/ntm_ctrl
 * - /proc/ntm/ntm_params
 */
int init_ntm_proc_entries(void) {
  /**   * create /proc/ntm directory   */
  parent_dir = proc_mkdir("ntm", NULL);
  if (!parent_dir) {
    pr_err("Failed to create /proc/ntm directory\n");
    return -ENOMEM;
  }
  /**   * create /proc/ntm/ntm_ctrl   */
  entry_ctrl = proc_create("ntm_ctrl", 0666, parent_dir, &ntm_ctrl_ops);
  if (!entry_ctrl) {
    pr_err("Failed to create proc entry for control\n");
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }
  /** create /proc/ntm/ntm_params */
  entry_params = proc_create("ntm_params", 0666, parent_dir, &ntm_params_ops);
  if (!entry_params) {
    pr_err("Failed to create proc entry for params\n");
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm", NULL);
    blk_unregister_tracepoints();
    vfree(raw_blk_stat);
    return -ENOMEM;
  }
  return 0;
}


static int __init init_ntm_module(void) {
  int ret;

  /**
   * initialize global variables
   * - record_enabled
   * - device_name
   */
  record_enabled = 0;
  device_name[0] = '\0';

  /** initialize the proc entries */
  ret = init_ntm_proc_entries();
  if (ret) return ret;

  /** setup the blk layer monitor */
  ret = _init_ntm_blk_layer();
  if (ret) {
    pr_err("Failed to initialize ntm_blk_layer\n");
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm_params", parent_dir);
    remove_proc_entry("ntm", NULL);
    return ret;
  }

  ret = _init_ntm_nvmetcp_layer();
  if(ret) {
    pr_err("Failed to initialize ntm_nvmetcp_layer\n");
    _exit_ntm_blk_layer();
    remove_proc_entry("ntm_ctrl", parent_dir);
    remove_proc_entry("ntm_params", parent_dir);
    remove_proc_entry("ntm", NULL);
    return ret;
  }
  return 0;
}

static void __exit exit_ntm_module(void) {
  if (update_routine_thread) {
    kthread_stop(update_routine_thread);
  }

  /** exit the blk layer monitor */
  _exit_ntm_blk_layer();

  _exit_ntm_nvmetcp_layer();

  remove_proc_entry("ntm_ctrl", parent_dir);
  remove_proc_entry("ntm_params", parent_dir);
  remove_proc_entry("ntm", NULL);
}

module_init(init_ntm_module);
module_exit(exit_ntm_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
