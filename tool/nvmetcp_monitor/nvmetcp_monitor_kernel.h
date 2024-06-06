#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/blk_types.h>
#include <linux/ktime.h>

#include "nvmetcp_monitor_com.h"

struct _blk_stat {
    /** every io */
    atomic64_t read_count;
    atomic64_t write_count;
    
    /** sampled io */
    atomic64_t read_io[9];
    atomic64_t write_io[9];

    /** just once */
    atomic64_t pending_rq;
};

enum SIZE_TYPE {
    _LT_4K,
    _4K,
    _8K,
    _16K,
    _32K,
    _64K,
    _128K,
    _GT_128K,
    _OTHERS
};

/** sliding window on time */
struct sliding_window {
  /** a lock free linked list of <timestamp, request> */
  struct list_head list;
  /** count */
  atomic64_t count;
  spinlock_t lock;
};

struct bio_info {
    struct list_head list;
    u64 ts;
    u64 size;
    u64 pos;
};

static void extract_bio_info(struct bio_info *info, struct bio *bio) {
    info->ts = ktime_get_ns();
    info->size = bio->bi_iter.bi_size;
    info->pos = bio->bi_iter.bi_sector;
}


static void _init_blk_tr(struct _blk_stat *tr) {
    atomic64_set(&tr->read_count, 0);
    atomic64_set(&tr->write_count, 0);
    int i;
    for (i = 0; i < 9; i++) {
        atomic64_set(&tr->read_io[i], 0);
        atomic64_set(&tr->write_io[i], 0);
    }
    atomic64_set(&tr->pending_rq, 0);
}

static void copy_blk_stat(struct blk_stat *dst, struct _blk_stat *src) {
    // spin_lock(&dst->lock);
    // read atomic data
    dst->read_count = atomic64_read(&src->read_count);
    dst->write_count = atomic64_read(&src->write_count);
    int i;
    for (i = 0; i < 9; i++) {
        dst->read_io[i] = atomic64_read(&src->read_io[i]);
        dst->write_io[i] = atomic64_read(&src->write_io[i]);
    }
    // dst->queue_length = atomic64_read(&src->queue_length);
    // spin_unlock(&dst->lock);
}

static void init_blk_tr(struct blk_stat *tr) {
    // spin_lock_init(&tr->lock);
    tr->read_count = 0;
    tr->write_count = 0;
    int i;
    for (i = 0; i < 9; i++) {
        tr->read_io[i] = 0;
        tr->write_io[i] = 0;
    }
    tr->pending_rq = 0;
}

static void init_sliding_window(struct sliding_window *sw) {
    INIT_LIST_HEAD(&sw->list);
    atomic64_set(&sw->count, 0);
    spin_lock_init(&sw->lock);
}