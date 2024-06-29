#include "k_blk_layer.h"

#include <linux/proc_fs.h>
#include <trace/events/block.h>

#include "k_ntm.h"

#include "util.h"

static atomic64_t sample_cnt;

/** in-kernel strucutre to update blk layer statistics */
static struct atomic_blk_stat *inner_blk_stat;

/** raw blk layer statistics, shared in user space*/
// static struct blk_stat *raw_blk_stat;
static struct shared_blk_layer_stat *shared_blk_layer_stat;

/**
 * communication entries
 */
struct proc_dir_entry *entry_blk_dir;
struct proc_dir_entry *entry_blk_stat;


static bool to_sample(void) {
  return atomic64_inc_return(&sample_cnt) % args->rate == 0;
}

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
 */
void inc_cnt_atomic_arr(atomic64_t *arr, int size) {
  atomic64_inc(&arr[size_to_enum(size)]);
}

void add_atomic_array(atomic64_t *arr, int size, u64 val) {
  atomic64_add(val, &arr[size_to_enum(size)]);
}

/**
 * initialize a atomic_blk_stat structure
 * set all atomic variables to 0
 * @param tr: the atomic_blk_stat to be initialized
 */
void init_atomic_blk_stat(struct atomic_blk_stat *tr) {
  int i;
  atomic64_set(&tr->read_count, 0);
  atomic64_set(&tr->write_count, 0);
  // atomic64_set(&tr->pending_rq, 0);
  atomic64_set(&tr->read_lat, 0);
  atomic64_set(&tr->write_lat, 0);
  for (i = 0; i < SIZE_NUM; i++) {
    atomic64_set(&tr->read_io[i], 0);
    atomic64_set(&tr->write_io[i], 0);
    atomic64_set(&tr->read_io_lat[i], 0);
    atomic64_set(&tr->write_io_lat[i], 0);
  }
}

/**
 * copy the data from atomic_blk_stat to blk_stat
 * This function is not thread safe
 */
void copy_blk_stat(struct blk_stat *dst, struct atomic_blk_stat *src) {
  int i;
  dst->read_count = atomic64_read(&src->read_count);
  dst->write_count = atomic64_read(&src->write_count);
  for (i = 0; i < SIZE_NUM; i++) {
    dst->read_io[i] = atomic64_read(&src->read_io[i]);
    dst->write_io[i] = atomic64_read(&src->write_io[i]);
    dst->read_io_lat[i] = atomic64_read(&src->read_io_lat[i]);
    dst->write_io_lat[i] = atomic64_read(&src->write_io_lat[i]);
  }
  dst->read_lat = atomic64_read(&src->read_lat);
  dst->write_lat = atomic64_read(&src->write_lat);
}



/** TODO: remove this function */
void on_block_rq_complete(void *ignore, struct request *rq, int err,
                          unsigned int nr_bytes) {
  return;
}

void on_block_bio_complete(void *ignore, struct request_queue *q,
                           struct bio *bio) {
  if (ctrl && args->io_type + bio_data_dir(bio) != 1) {
    if (bio->bi_bdev == NULL || bio->bi_bdev->bd_disk == NULL) return;
    // pr_info("dev: %s", bio->bi_bdev->bd_disk->disk_name); -- dev: nvme4c4n1
    if (!is_same_dev_name(bio->bi_bdev->bd_disk->disk_name, args->dev)) return;

    if (to_sample()) {
      /** update inner_blk_stat to record all sampled bio */
      u64 _start = bio_issue_time(&bio->bi_issue);
      u64 _now = __bio_issue_time(ktime_get_ns());
      u64 lat = _now - _start;

      /** read the size of the io */
      unsigned int size = bio->bi_iter.bi_size;

      /** read the io direction and increase the corresponding counter */
      if (bio_data_dir(bio) == READ) {
        atomic64_inc(&inner_blk_stat->read_count);
        inc_cnt_atomic_arr(inner_blk_stat->read_io, size);
        add_atomic_array(inner_blk_stat->read_io_lat, size, lat);
        atomic64_add(lat, &inner_blk_stat->read_lat);
      } else if (bio_data_dir(bio) == WRITE) {
        inc_cnt_atomic_arr(inner_blk_stat->write_io, size);
        atomic64_inc(&inner_blk_stat->write_count);
        atomic64_add(lat, &inner_blk_stat->write_lat);
        add_atomic_array(inner_blk_stat->write_io_lat, size, lat);
      } else {
        pr_err("Unexpected bio direction, neither READ nor WRITE.\n");
      }
    }
    bio = bio->bi_next;
  }
}

/**
 * register the tracepoints
 * - block_bio_queue
 */
static int blk_register_tracepoints(void) {
  int ret;
  ret = register_trace_block_rq_complete(on_block_rq_complete, NULL);
  if (ret) goto failed;

  ret = register_trace_block_bio_complete(on_block_bio_complete, NULL);
  if (ret) goto unregister_block_rq_complete;
  return 0;

unregister_block_rq_complete:
  unregister_trace_block_rq_complete(on_block_rq_complete, NULL);

failed:
  return ret;
}

/**
 * unregister the tracepoints
 * - block_bio_queue
 */
static void blk_unregister_tracepoints(void) {
  unregister_trace_block_rq_complete(on_block_rq_complete, NULL);
  unregister_trace_block_bio_complete(on_block_bio_complete, NULL);
}

/**
 * This function defines how user space map the a variable to
 * the kernel space variable, raw_blk_stat
 * @param filp: the file to map
 * @param vma: the virtual memory area to map
 * @return 0 if success, -EAGAIN if failed
 */
static int mmap_shared_blk_layer_stat(struct file *filp,
                                      struct vm_area_struct *vma) {
  if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(shared_blk_layer_stat),
                      vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;
  return 0;
}

/**
 * define the operations for the raw_blk_stat command
 * - proc_mmap: map the raw_blk_stat to the user space
 */
static const struct proc_ops ntm_shared_blk_layer_stat_ops = {
    .proc_mmap = mmap_shared_blk_layer_stat,
};

/**
 * update the blk layer statistic periodically
 * This function will be triggered in the main routine thread
 */
void blk_layer_update(u64 now) {
  /** update the raw blk layer statistic in the user space */
  copy_blk_stat(&shared_blk_layer_stat->all_time_stat, inner_blk_stat);
}

/**
 * initialize the variables
 * - set ctrl to 0
 * - set device_name to empty string
 * - allocate memory for _raw_blk_stat, raw_blk_stat, sw, sample
 * - initialize the blk_stat structures
 */
int init_blk_layer_variables(void) {
  atomic64_set(&sample_cnt, 0);
  /** _raw_blk_stat */
  inner_blk_stat = kmalloc(sizeof(*inner_blk_stat), GFP_KERNEL);
  if (!inner_blk_stat) return -ENOMEM;
  init_atomic_blk_stat(inner_blk_stat);

  /** shared_blk_layer_stat */
  shared_blk_layer_stat = vmalloc(sizeof(*shared_blk_layer_stat));
  if (!shared_blk_layer_stat) return -ENOMEM;
  init_blk_stat(&shared_blk_layer_stat->all_time_stat);

  return 0;
}

int free_blk_layer_variables(void) {
  if (inner_blk_stat) kfree(inner_blk_stat);
  if (shared_blk_layer_stat) vfree(shared_blk_layer_stat);
  return 0;
}

static char *dir_name = "blk";
static char *stat_name = "stat";

/**
 * initialize the proc entries
 * - create /proc/ntm/blk dir
 * - create /proc/ntm/blk/stat entry
 *
 * make sure if one of the proc entries is not created, remove the previous
 * created entries
 */
int init_blk_layer_proc_entries(void) {
  entry_blk_dir = proc_mkdir(dir_name, parent_dir);
  if (!entry_blk_dir) goto failed;

  entry_blk_stat = proc_create(stat_name, 0666, entry_blk_dir,
                               &ntm_shared_blk_layer_stat_ops);
  if (!entry_blk_stat) goto remove_blk_dir;

  return 0;

remove_blk_dir:
  remove_proc_entry(dir_name, parent_dir);
  entry_blk_dir = NULL;
failed:
  return -ENOMEM;
}

/**
 * remove the proc entries
 * - remove /proc/ntm/stat
 * - remove /proc/ntm/blk dir
 */
static void remove_blk_layer_proc_entries(void) {
  if (entry_blk_stat) remove_proc_entry(stat_name, entry_blk_dir);
  if (entry_blk_dir) remove_proc_entry(dir_name, parent_dir);
}

/**
 * initialize the ntm blk layer
 * - initialize the variables
 * - initialize the proc entries
 * - register the tracepoints
 */
int init_blk_layer_monitor(void) {
  int ret;
  ret = init_blk_layer_variables();
  if (ret) return ret;
  ret = init_blk_layer_proc_entries();
  if (ret) return ret;

  ret = blk_register_tracepoints();
  if (ret) return ret;
  return 0;
}

/**
 * exit the ntm blk layer
 * - remove the proc entries
 * - unregister the tracepoints
 * - clean the variables
 */
void exit_blk_layer_monitor(void) {
  remove_blk_layer_proc_entries();
  blk_unregister_tracepoints();
  free_blk_layer_variables();
}
