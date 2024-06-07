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




struct blk_stat_set {
  struct blk_stat *raw_blk_stat;
  struct blk_stat *blk_stat_10s;
  struct blk_stat *blk_stat_2s;
};

static struct blk_stat_set *blk_set;
static char* blk_dev_name;

void print_blk_stat_set(struct blk_stat_set *bs, bool clear);
void serialize_blk_tr(struct blk_stat *tr, FILE *file);
void deserialize_blk_tr(struct blk_stat *tr, FILE *file);
void init_ntm_blk(char *dev_name, struct blk_stat_set *bs);
void start_ntm_blk();
void exit_ntm_blk();
