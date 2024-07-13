#include "k_blk_layer.h"

#include <linux/bio.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/types.h>
#include <trace/events/block.h>

#include "k_nttm.h"
#include "nttm_com.h"
#include "util.h"

static atomic64_t sample_cnt;

static struct proc_dir_entry *entry_blk_dir;
static struct proc_dir_entry *entry_blk_stat;

static struct blk_stat *blk_stat;

static struct atomic_blk_stat *raw_blk_stat;

static char *dir_name = "blk";
static char *stat_name = "stat";

bool blk_to_sample(void) {
  return atomic64_inc_return(&sample_cnt) % args->rate == 0;
}

// void init_blk_io_instance(struct blk_io_instance *bio) {
//   bio->is_write = false;
//   bio->bio = NULL;
//   bio->cnt = 0;
//   bio->size = 0;
//   bio->is_spoiled = false;
// }

// void append_blk_event(struct blk_io_instance *bio, u64 ts, enum blk_trpt trpt) {
//   if (bio->cnt >= BLK_EVENT_NUM) {
//     bio->is_spoiled = true;
//     pr_err("blk_io_instance is spoiled\n");
//     return;
//   }
//   bio->ts[bio->cnt] = ts;
//   bio->trpt[bio->cnt] = trpt;
//   bio->cnt++;
//   return;
// }

// void print_blk_io_instance(struct blk_io_instance *bio) {
//   int i;
//   char trpt_name[32];
//   pr_info("blk_io_instance: \t");
//   pr_info("is_write: %d\t", bio->is_write);
//   pr_info("bio: %p\t", bio->bio);
//   pr_info("cnt: %d\t", bio->cnt);
//   pr_info("size: %d\t", bio->size);
//   pr_info("is_spoiled: %d\t", bio->is_spoiled);
//   for (i = 0; i < bio->cnt; i++) {
//     blk_trpt_name(bio->trpt[i], trpt_name);
//     pr_info("ts: %llu, trpt: %s\n", bio->ts[i], trpt_name);
//   }
// }

void inc_cnt_atomic_arr(atomic64_t *arr, int size) {
  atomic64_inc(&arr[size_to_enum(size)]);
}

void add_atomic_arr(atomic64_t *arr, int size, unsigned long long val) {
  atomic64_add(val, &arr[size_to_enum(size)]);
}

void copy_blk_stat(struct blk_stat *dst, struct atomic_blk_stat *src) {
  int i;
  dst->all_read_cnt = atomic64_read(&src->read_cnt);
  dst->all_write_cnt = atomic64_read(&src->write_cnt);
  for (i = 0; i < SIZE_NUM; i++) {
    dst->all_read_io[i] = atomic64_read(&src->read_io[i]);
    dst->all_write_io[i] = atomic64_read(&src->write_io[i]);
    dst->all_read_time[i] = atomic64_read(&src->read_time[i]);
    dst->all_write_time[i] = atomic64_read(&src->write_time[i]);
  }
}

/**
 * this function update the variable shared by user space and the kernel space
 */
void blk_layer_update(u64 now) { copy_blk_stat(blk_stat, raw_blk_stat); }

// /**
//  * This function is to update the raw_blk_stat, which record all-time
//  * statistics. This function is called when there is a sample bio completed.
//  * @param bio: the completed bio
//  * @param raw_blk_stat: the raw_blk_stat
//  */
// void udpate_raw_blk_stat(struct blk_io_instance *bio,
//                          struct atomic_blk_stat *raw_blk_stat) {
//   unsigned int size;

//   size = bio->size;
//   if (bio->is_write) {
//     atomic64_inc(&raw_blk_stat->write_cnt);
//     add_atomic_arr(raw_blk_stat->write_time, size, bio->ts[1] - bio->ts[0]);
//     inc_cnt_atomic_arr(raw_blk_stat->write_io, size);
//   } else {
//     atomic64_inc(&raw_blk_stat->read_cnt);
//     add_atomic_arr(raw_blk_stat->read_time, size, bio->ts[1] - bio->ts[0]);
//     inc_cnt_atomic_arr(raw_blk_stat->read_io, size);
//   }
// }

void inc_raw_blk_stat(u64 start, u64 now, bool is_write, bool size,
                      struct atomic_blk_stat *raw_blk_stat) {
  if (is_write) {
    atomic64_inc(&raw_blk_stat->write_cnt);
    add_atomic_arr(raw_blk_stat->write_time, size, now - start);
    inc_cnt_atomic_arr(raw_blk_stat->write_io, size);
  } else {
    atomic64_inc(&raw_blk_stat->read_cnt);
    add_atomic_arr(raw_blk_stat->read_time, size, now - start);
    inc_cnt_atomic_arr(raw_blk_stat->read_io, size);
  }
}

/** TODO: remove this function */
void on_block_rq_complete(void *ignore, struct request *rq, int err,
                          unsigned int nr_bytes) {
  if (ctrl && args->io_type + rq_data_dir(rq) != 1) {
    struct bio *bio;
    if (rq->rq_disk == NULL) return;
    if (!is_same_dev_name(rq->rq_disk->disk_name, args->dev)) return;

    bio = rq->bio;
    while (bio) {
      if (blk_to_sample()) {
        /** update inner_blk_stat to record all sampled bio */
        u64 _start = bio_issue_time(&bio->bi_issue);
        u64 _now = __bio_issue_time(ktime_get_ns());
        u64 lat = _now - _start;

        /** read the size of the io */
        unsigned int size = bio->bi_iter.bi_size;
        // pr_info("size: %u, lat: %llu\n", size, lat);

        /** read the io direction and increase the corresponding counter */
        if (bio_data_dir(bio) == READ) {
          atomic64_inc(&raw_blk_stat->read_cnt);
          inc_cnt_atomic_arr(raw_blk_stat->read_io, size);
          add_atomic_arr(raw_blk_stat->read_time, size, lat);
        } else if (bio_data_dir(bio) == WRITE) {
          inc_cnt_atomic_arr(raw_blk_stat->write_io, size);
          atomic64_inc(&raw_blk_stat->write_cnt);
          add_atomic_arr(raw_blk_stat->write_time, size, lat);
        } else {
          pr_err("Unexpected bio direction, neither READ nor WRITE.\n");
        }
      }
      bio = bio->bi_next;
    }
  }
  return;
}

/** it is difficult to match bio to the previous recorded bio since there is no unique id */
/** TODO: to remove this function */
void on_block_bio_complete(void *ignore, struct request_queue *,
                           struct bio *bio) {
 return;
}

static int blk_register_tracepoints(void) {
  int ret;
  pr_info("Registering tracepoints\n");

  pr_info("Registering tracepoint: blk_rq_complete\n");
  ret = register_trace_block_rq_complete(on_block_rq_complete, NULL);
  if (ret) goto failed;

  return 0;


failed:
  pr_err("Failed to register tracepoints\n");
  return ret;
}

static int blk_unregister_tracepoints(void) {
  pr_info("Unregistering tracepoints\n");
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
  entry_blk_dir = proc_mkdir(dir_name, parent_dir);
  if (!entry_blk_dir) {
    pr_err("Failed to create %s directory\n", dir_name);
    goto failed;
  }

  entry_blk_stat =
      proc_create(stat_name, 0, entry_blk_dir, &nttm_blk_stat_fops);
  if (!entry_blk_stat) {
    pr_err("Failed to create proc entry for %s\n", stat_name);
    goto cleanup_blk_dir;
  }
  return 0;

cleanup_blk_dir:
  remove_proc_entry(dir_name, parent_dir);
failed:
  return -ENOMEM;
}

static void remove_blk_layer_proc_entries(void) {
  remove_proc_entry(stat_name, entry_blk_dir);
  remove_proc_entry(dir_name, parent_dir);
}

int init_blk_layer_variables(void) {
  atomic64_set(&sample_cnt, 0);
  blk_stat = vmalloc(sizeof(struct blk_stat));
  if (!blk_stat) {
    pr_err("Failed to allocate memory for blk_stat\n");
    return -ENOMEM;
  }
  init_blk_stat(blk_stat);

  raw_blk_stat = kmalloc(sizeof(struct atomic_blk_stat), GFP_KERNEL);
  if (!raw_blk_stat) {
    pr_err("Failed to allocate memory for raw_blk_stat\n");
    return -ENOMEM;
  }
  init_atomic_blk_stat(raw_blk_stat);

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
