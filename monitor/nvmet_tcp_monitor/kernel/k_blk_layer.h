#ifndef _K_BLK_LAYER_H_
#define _K_BLK_LAYER_H_

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/string.h>

#include "nttm_com.h"

#define BLK_EVENT_NUM 2

enum blk_trpt { BIO_QUEUE, BIO_COMPLETE };

static inline void blk_trpt_name(enum blk_trpt trpt, char *name) {
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

// struct blk_io_instance {
//   bool is_write;
//   struct bio *bio;
//   u64 ts[BLK_EVENT_NUM];
//   enum blk_trpt trpt[BLK_EVENT_NUM];
//   u8 cnt;
//   u32 size;
//   bool is_spoiled;
// };

struct atomic_blk_stat {
  atomic64_t read_cnt;
  atomic64_t write_cnt;
  atomic64_t read_io[SIZE_NUM];
  atomic64_t write_io[SIZE_NUM];
  atomic64_t read_time[SIZE_NUM];
  atomic64_t write_time[SIZE_NUM];
  // atomic64_t in_flight;
};

static inline void init_atomic_blk_stat(struct atomic_blk_stat *tr) {
  int i;
  atomic64_set(&tr->read_cnt, 0);
  atomic64_set(&tr->write_cnt, 0);
  for (i = 0; i < SIZE_NUM; i++) {
    atomic64_set(&tr->read_io[i], 0);
    atomic64_set(&tr->write_io[i], 0);
  }
}


void blk_layer_update(u64 now);

int init_blk_layer(void) ;

void exit_blk_layer(void) ;

#endif  // _K_BLK_LAYER_H_