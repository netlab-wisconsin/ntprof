#ifndef K_NTM_H
#define K_NTM_H

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>

#include <linux/string.h>

#include "ntm_com.h"

/** for storing the device name */
// static char device_name[32] = "";

static Arguments* args;

/** a thread, periodically update the communication data strucure */
static struct task_struct *update_routine_thread;

/** inticator, to record or not */
static int ctrl = 0;


/**
 * communication entries
 */
struct proc_dir_entry *parent_dir;
struct proc_dir_entry *entry_ctrl;
struct proc_dir_entry *entry_args;

/**
 * Given a device name, return the request queue of the device.
 * @param dev_name the device name, e.g., nvme0n1
 * @return the request queue of the device, NULL if failed
*/
struct request_queue *device_name_to_queue(const char *dev_name) {
  struct block_device *bdev;
  struct request_queue *q = NULL;

  /** add /dev/ prefix */
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

int qid2port[MAX_QID];

#endif // K_NTM_H