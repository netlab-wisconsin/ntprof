#ifndef _K_BLK_LAYER_H_
#define _K_BLK_LAYER_H_

#include <linux/bio.h>
#include <linux/time.h>
#include <linux/types.h>
#include <trace/events/block.h>

#define BLK_EVENT_NUM 2

atomic64_t blk_sample_cnt;

bool blk_to_sample(void) {
  return atomic64_inc_return(&blk_sample_cnt) % args->rate == 0;
}

enum blk_trpt { BIO_QUEUE, BIO_COMPLETE };

void blk_trpt_name(enum blk_trpt trpt, char *name) {
  switch (trpt) {
    case BIO_QUEUE:
      strcpy(name, "BIO_QUEUE");
      break;
    case BIO_COMPLETE:
      strcpy(name, "BIO_COMPLETE");
      break;
    default:
      strcpy(name, "UNKNOWN");
      break;
  }
}

struct blk_io_instance {
  bool is_write;
  struct bio *bio;
  u64 ts[BLK_EVENT_NUM];
  enum blk_trpt trpt[BLK_EVENT_NUM];
  u8 cnt;
  u32 size;
  bool is_spoiled;
};

void init_blk_io_instance(struct blk_io_instance *bio) {
  bio->is_write = false;
  bio->bio = NULL;
  bio->cnt = 0;
  bio->size = 0;
  bio->is_spoiled = false;
}

void append_blk_event(struct blk_io_instance *bio, u64 ts, enum blk_trpt trpt) {
  if (bio->cnt >= BLK_EVENT_NUM) {
    bio->is_spoiled = true;
    pr_err("blk_io_instance is spoiled\n");
    return;
  }
  bio->ts[bio->cnt] = ts;
  bio->trpt[bio->cnt] = trpt;
  bio->cnt++;
  return;
}

void print_blk_io_instance(struct blk_io_instance *bio) {
  int i;
  char trpt_name[32];
  pr_info("blk_io_instance: \t");
  pr_info("is_write: %d\t", bio->is_write);
  pr_info("bio: %p\t", bio->bio);
  pr_info("cnt: %d\t", bio->cnt);
  pr_info("size: %d\t", bio->size);
  pr_info("is_spoiled: %d\t", bio->is_spoiled);
  for (i = 0; i < bio->cnt; i++) {
    blk_trpt_name(bio->trpt[i], trpt_name);
    pr_info("ts: %llu, trpt: %s\n", bio->ts[i], trpt_name);
  }
}

static struct blk_io_instance *current_bio = NULL;

static struct proc_dir_entry *entry_blk_dir;
static struct proc_dir_entry *entry_blk_stat;

static struct blk_stat *blk_stat;

struct _blk_stat {
  atomic64_t read_cnt;
  atomic64_t write_cnt;
  atomic64_t read_io[9];
  atomic64_t write_io[9];
  atomic64_t read_time[9];
  atomic64_t write_time[9];
  // atomic64_t in_flight;
};

static struct _blk_stat *raw_blk_stat;

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
  } else if (size > 131072) {
    atomic64_inc(&arr[_GT_128K]);
  } else {
    atomic64_inc(&arr[_OTHERS]);
  }
}

void add_atomic_arr(atomic64_t *arr, int size, unsigned long long val) {
  if (size < 4096) {
    atomic64_add(val, &arr[_LT_4K]);
  } else if (size == 4096) {
    atomic64_add(val, &arr[_4K]);
  } else if (size == 8192) {
    atomic64_add(val, &arr[_8K]);
  } else if (size == 16384) {
    atomic64_add(val, &arr[_16K]);
  } else if (size == 32768) {
    atomic64_add(val, &arr[_32K]);
  } else if (size == 65536) {
    atomic64_add(val, &arr[_64K]);
  } else if (size == 131072) {
    atomic64_add(val, &arr[_128K]);
  } else if (size > 131072) {
    atomic64_add(val, &arr[_GT_128K]);
  } else {
    atomic64_add(val, &arr[_OTHERS]);
  }

}

void _init_blk_tr(struct _blk_stat *tr) {
  int i;
  atomic64_set(&tr->read_cnt, 0);
  atomic64_set(&tr->write_cnt, 0);
  for (i = 0; i < 9; i++) {
    atomic64_set(&tr->read_io[i], 0);
    atomic64_set(&tr->write_io[i], 0);
  }
  // atomic64_set(&tr->in_flight, 0);
}

void copy_blk_stat(struct blk_stat *dst, struct _blk_stat *src) {
  int i;
  dst->all_read_cnt = atomic64_read(&src->read_cnt);
  dst->all_write_cnt = atomic64_read(&src->write_cnt);
  for (i = 0; i < 9; i++) {
    dst->all_read_io[i] = atomic64_read(&src->read_io[i]);
    dst->all_write_io[i] = atomic64_read(&src->write_io[i]);
    dst->all_read_time[i] = atomic64_read(&src->read_time[i]);
    dst->all_write_time[i] = atomic64_read(&src->write_time[i]);
  }
  // dst->all_read_time = atomic64_read(&src->read_time);
  // dst->in_flight = atomic64_read(&src->in_flight);
}

/**
 * this function update the variable shared by user space and the kernel space
 */
void blk_layer_update(u64 now) {
  copy_blk_stat(blk_stat, raw_blk_stat);
}

void on_block_bio_queue(void *ignore, struct bio *bio) {
  if (ctrl && args->io_type + bio_data_dir(bio) != 1) {
    atomic64_t *arr = NULL;
    unsigned int size;
    const char *rq_disk_name = bio->bi_bdev->bd_disk->disk_name;
    if (strcmp(rq_disk_name, args->dev) != 0 &&
        strcmp(args->dev, "all devices") != 0) {
      return;
    }
    if (blk_to_sample()) {
      /** update current bio if it is NULL */
      if (current_bio == NULL) {
        current_bio = kmalloc(sizeof(struct blk_io_instance), GFP_KERNEL);
        init_blk_io_instance(current_bio);
        current_bio->is_write = bio_data_dir(bio);
        current_bio->bio = bio;
        current_bio->size = bio->bi_iter.bi_size;
        append_blk_event(current_bio, ktime_get_ns(), BIO_QUEUE);
      } else {
        pr_info("current_bio is not NULL\n");
      }
    }
  }
}



/**
 * This function is to update the raw_blk_stat, which record all-time statistics.
 * This function is called when there is a sample bio completed. 
 * @param bio: the completed bio
 * @param raw_blk_stat: the raw_blk_stat
 */
void udpate_raw_blk_stat(struct blk_io_instance *bio, struct _blk_stat *raw_blk_stat) {
  atomic64_t *arr = NULL;
  unsigned int size;

  size = bio->size;
  if (bio->is_write) {
    atomic64_inc(&raw_blk_stat->write_cnt);
    add_atomic_arr(raw_blk_stat->write_time, size, bio->ts[1] - bio->ts[0]);
    inc_cnt_atomic_arr(raw_blk_stat->write_io, size);
  } else {
    atomic64_inc(&raw_blk_stat->read_cnt);
    add_atomic_arr(raw_blk_stat->read_time, size, bio->ts[1] - bio->ts[0]);
    inc_cnt_atomic_arr(raw_blk_stat->read_io, size);
  }
}

void on_block_rq_complete(void *ignore, struct request *rq, int err,
                          unsigned int nr_bytes) {
  // if (ctrl && args->io_type + rq_data_dir(rq) != 1) {

  //   /** if the device name of the request is different from args, return */
  //   if (rq->bio == NULL || rq->bio->bi_bdev == NULL || rq->bio->bi_bdev->bd_disk == NULL) return;
  //   struct bio *bio = rq->bio;
  //   if (!is_same_dev_name(bio->bi_bdev->bd_disk->disk_name, args->dev)) return;

  //   while (bio) {
  //     if (current_bio && bio == current_bio->bio) {
  //       append_blk_event(current_bio, ktime_get_ns(), BIO_COMPLETE);
  //       if (current_bio->is_spoiled) {
  //         pr_err("current_bio is spoiled\n");
  //       } else {
  //         /** update the blk_stat, all time part */
  //         udpate_raw_blk_stat(current_bio, raw_blk_stat);
  //       }
  //       current_bio = NULL;
  //     }
  //     // atomic64_dec(&raw_blk_stat->in_flight);
    
  //     bio = bio->bi_next;
  //   }
  // }
}

void on_block_bio_complete(void *ignore, struct request_queue *,
                           struct bio *bio) {
  if(ctrl && args->io_type + bio_data_dir(bio) != 1) {
    if (bio->bi_bdev == NULL || bio->bi_bdev->bd_disk == NULL) return;
    if (!is_same_dev_name(bio->bi_bdev->bd_disk->disk_name, args->dev)) return;
    if (current_bio && bio == current_bio->bio) {
      append_blk_event(current_bio, ktime_get_ns(), BIO_COMPLETE);
      if (current_bio->is_spoiled) {
        pr_err("current_bio is spoiled\n");
      } else {
        /** update the blk_stat, all time part */
        udpate_raw_blk_stat(current_bio, raw_blk_stat);
      }
      current_bio = NULL;
    }
  }
}

static int blk_register_tracepoints(void) {
  int ret;
  pr_info("Registering tracepoints\n");

  pr_info("Registering tracepoint: blk_bio_queue\n");
  ret = register_trace_block_bio_queue(on_block_bio_queue, NULL);
  if (ret) goto failed;

  pr_info("Registering tracepoint: blk_bio_complete\n");
  ret = register_trace_block_bio_complete(on_block_bio_complete, NULL);
  if (ret) goto unregister_bio_queue;

  pr_info("Registering tracepoint: blk_rq_complete\n");
  ret = register_trace_block_rq_complete(on_block_rq_complete, NULL);
  if (ret) goto unregister_bio_complete;

  return 0;

unregister_bio_complete:
  unregister_trace_block_bio_complete(on_block_bio_complete, NULL);

unregister_bio_queue:
  unregister_trace_block_bio_queue(on_block_bio_queue, NULL);

failed:
  pr_err("Failed to register tracepoints\n");
  return ret;
}

static int blk_unregister_tracepoints(void) {
  pr_info("Unregistering tracepoints\n");
  unregister_trace_block_bio_queue(on_block_bio_queue, NULL);
  unregister_trace_block_bio_complete(on_block_bio_complete, NULL);
  unregister_trace_block_rq_complete(on_block_rq_complete, NULL);
  return 0;
}

static int mmap_blk_stat(struct file *file, struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(blk_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

static const struct proc_ops nttm_blk_stat_fops = {
    .proc_mmap = mmap_blk_stat,
};

int init_blk_layer_proc_entries(void) {
  pr_info("Initializing blk proc entries\n");
  entry_blk_dir = proc_mkdir("blk", parent_dir);
  if (!entry_blk_dir) {
    pr_err("Failed to create /proc/nttm/blk directory\n");
    goto failed;
  }

  entry_blk_stat = proc_create("stat", 0, entry_blk_dir, &nttm_blk_stat_fops);
  if (!entry_blk_stat) {
    pr_err("Failed to create proc entry for stat\n");
    goto cleanup_blk_dir;
  }
  return 0;

cleanup_blk_dir:
  remove_proc_entry("blk", parent_dir);
failed:
  return -ENOMEM;
}

static void remove_blk_layer_proc_entries(void) {
  remove_proc_entry("stat", entry_blk_dir);
  remove_proc_entry("blk", parent_dir);
}

int init_blk_layer_variables(void) {
  atomic64_set(&blk_sample_cnt, 0);
  blk_stat = vmalloc(sizeof(struct blk_stat));
  if (!blk_stat) {
    pr_err("Failed to allocate memory for blk_stat\n");
    return -ENOMEM;
  }
  init_blk_stat(blk_stat);

  raw_blk_stat = kmalloc(sizeof(struct _blk_stat), GFP_KERNEL);
  if (!raw_blk_stat) {
    pr_err("Failed to allocate memory for raw_blk_stat\n");
    return -ENOMEM;
  }
  _init_blk_tr(raw_blk_stat);

  return 0;
}

void free_blk_varialbes(void) {
  vfree(blk_stat);
  kfree(raw_blk_stat);
}

int init_blk_layer(void) {
  int ret;
  pr_info("Initializing blk layer\n");
  ret = init_blk_layer_variables();
  if (ret) return ret;
  ret = init_blk_layer_proc_entries();
  if (ret) return ret;
  ret = blk_register_tracepoints();
  if (ret) return ret;
  return 0;
}

void exit_blk_layer(void) {
  pr_info("Exiting blk layer\n");
  remove_blk_layer_proc_entries();
  free_blk_varialbes();
  blk_unregister_tracepoints();
}

#endif  // _K_BLK_LAYER_H_