#ifndef U_BLK_LAYER_H
#define U_BLK_LAYER_H

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
 * - blk_stat: the statistics in the last x seconds
*/
struct blk_stat_set {
  struct blk_stat *raw_blk_stat;
  struct blk_stat *blk_stat;
};

static struct blk_stat_set blk_set;

void print_blk_stat_set();

void init_ntm_blk();

void map_ntm_blk_data();

void unmap_ntm_blk_data();

#endif // U_BLK_LAYER_H