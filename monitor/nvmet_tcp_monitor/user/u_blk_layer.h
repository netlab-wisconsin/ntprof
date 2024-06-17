#ifndef U_BLK_LAYER_H
#define U_BLK_LAYER_H

#include <stdio.h>

#include "nttm_com.h"

static struct blk_stat *blk_stat;

void print_blk_sample_summary(struct blk_sample_summary *summary) {
  printf("sample summary: \t");
  if (summary->cnt) {
    printf("cnt: %lu\t", summary->cnt);
    printf("total_time(us): %.6f\t", (float)summary->total_time / 1000);
    printf("average_time(us): %.6f\n",
           (float)summary->total_time / 1000 / summary->cnt);
  } else {
    printf("cnt: %lu\n", summary->cnt);
  }
}

void print_blk_stat(struct blk_stat *b_stat, char *header) {
  printf("header: %s \t", header);
  printf("device_name: %s\n", args->dev);
  char *dis_header[9] = {"<4KB", "4KB",   "8KB",    "16KB",  "32KB",
                         "64KB", "128KB", ">128KB", "others"};
  printf("read total: %lu\n", b_stat->read_cnt);
  printf("read cnt");
  int i;
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %lu]", dis_header[i], b_stat->read_io[i]);
  }
  printf("\nread dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->read_io[i] / b_stat->read_cnt);
  }
  printf("\nwrite total: %lu\n", b_stat->write_cnt);
  printf("write cnt");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %lu]", dis_header[i], b_stat->write_io[i]);
  }
  printf("\nwrite dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->write_io[i] / b_stat->write_cnt);
  }
  printf("\n");

  printf("in_flight: %d\n", b_stat->in_flight);
  print_blk_sample_summary(&b_stat->sample_summary);

  printf("\n\n");
}

void print_blk_layer_stat() {
  if (blk_stat) {
    print_blk_stat(blk_stat, "BLK STAT");
  } else {
    printf("blk_stat is NULL\n");
  }
}

void map_nttm_blk_data() {
  int fd;

  fd = open("/proc/nttm/blk/stat", O_RDONLY);
  if (fd) printf("failed to open /proc/nttm/blk/stat\n");

  blk_stat = (struct blk_stat *)mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                                     MAP_SHARED, fd, 0);
  if (blk_stat == MAP_FAILED) {
    printf("failed to map /proc/nttm/blk/stat\n");
  }
  close(fd);
}

void unmap_nttm_blk_data() {
  if (blk_stat) munmap(blk_stat, sizeof(struct blk_stat));
}

#endif  // U_BLK_LAYER_H