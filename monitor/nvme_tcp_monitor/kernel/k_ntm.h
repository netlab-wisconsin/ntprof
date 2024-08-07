#ifndef K_NTM_H
#define K_NTM_H

#include "config.h"

extern Arguments* args;

/** inticator, to record or not */
extern int ctrl;

/**
 * communication entries
 */
extern struct proc_dir_entry *parent_dir;

extern int cwnds[MAX_QID];

// /**
//  * Given a device name, return the request queue of the device.
//  * @param dev_name the device name, e.g., nvme0n1
//  * @return the request queue of the device, NULL if failed
// */
// static struct request_queue *device_name_to_queue(const char *dev_name) {
//   struct block_device *bdev;
//   struct request_queue *q = NULL;

//   /** add /dev/ prefix */
//   char full_name[32];
//   snprintf(full_name, sizeof(full_name), "/dev/%s", dev_name);

//   bdev = blkdev_get_by_path(full_name, FMODE_READ, NULL);
//   if (IS_ERR(bdev)) {
//     pr_err("Failed to get block device for device %s\n", full_name);
//     return NULL;
//   }

//   q = bdev_get_queue(bdev);
//   if (!q) {
//     pr_err("Failed to get request queue for device %s\n", full_name);
//   }

//   blkdev_put(bdev, FMODE_READ);
//   return q;
// }

extern int qid2port[MAX_QID];

#endif // K_NTM_H