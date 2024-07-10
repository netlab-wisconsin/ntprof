#ifndef _K_BLK_LAYER_H_
#define _K_BLK_LAYER_H_

#include <linux/kernel.h>
#include <linux/types.h>

#include "ntm_com.h"


/**
 * blk layer statistics, with atomic variables
 * This is used for recording raw data in the kernel space
 */
struct atomic_blk_stat {
  /** total number of read io */
  atomic64_t read_count;
  /** total number of write io */
  atomic64_t write_count;
  /**
   * read io number of different sizes
   * the sizs are divided into 9 categories
   * refers to enum size_type in ntm_com.h
   */
  atomic64_t read_io[SIZE_NUM];
  /** write io number of different sizes */
  atomic64_t write_io[SIZE_NUM];

  /** overall read and write lat */
  atomic64_t read_lat;
  atomic64_t write_lat;

  /** read and write lat for different type of io */
  atomic64_t read_io_lat[SIZE_NUM];
  atomic64_t write_io_lat[SIZE_NUM];

  /** TODO: number of io in-flight */
  // atomic64_t pending_rq;
};


/**
 * update the blk layer statistic periodically
 * This function will be triggered in the main routine thread
 */
void blk_layer_update(u64 now);

/**
 * initialize the ntm blk layer
 * - initialize the variables
 * - initialize the proc entries
 * - register the tracepoints
 */
int init_blk_layer_monitor(void) ;

/**
 * exit the ntm blk layer
 * - remove the proc entries
 * - unregister the tracepoints
 * - clean the variables
 */
void exit_blk_layer_monitor(void) ;

#endif  // _K_BLK_LAYER_H_