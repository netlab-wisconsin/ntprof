#ifndef BLK_LAYER_USER_H
#define BLK_LAYER_USER_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include "ntm_com.h"

/**
 * blk layer statistics set
 * - raw_blk_stat: the raw statistics
 * - blk_stat_10s: the statistics in the last 10s
 * - blk_stat_2s: the statistics in the last 2s
*/
struct blk_stat_set {
  struct blk_stat *raw_blk_stat;
  struct blk_stat *blk_stat_10s;
  struct blk_stat *blk_stat_2s;
};

static struct blk_stat_set *blk_set;
static char* blk_dev_name;

void print_blk_stat_set(struct blk_stat_set *bs, bool clear);

void init_ntm_blk(char *dev_name, struct blk_stat_set *bs);

void map_ntm_blk_data();

void unmap_ntm_blk_data();

#endif // BLK_LAYER_USER_H