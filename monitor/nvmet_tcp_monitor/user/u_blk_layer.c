#include "u_blk_layer.h"
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "nttm_com.h"
#include "output.h"
#include "u_nttm.h"


static struct blk_stat *blk_stat;


void print_blk_stat(struct blk_stat *b_stat) {
  printf(HEADER1 "[BLK LAYER]:\n" RESET);
  printf("device_name: %s\n", args->dev);
  printf(HEADER2 "all time:\n" RESET);
  printf(HEADER3 "\t [read lat] \t" RESET);
  printf("total bio: %llu, ", b_stat->all_read_cnt);
  printf("avg lat(us)");
  int i;
  for(i = 0; i < READ_SIZE_NUM; i++) {
    printf(", [%s: %.2f]", read_size_name(i), (float) b_stat->all_read_time[i] / 1000 / b_stat->all_read_io[i]);
  }
  printf("\n");
  
  printf(HEADER3 "\t [read dist] \t" RESET);
  for (i = 0; i < READ_SIZE_NUM; i++) {
    printf(" [%s: %.2f]", read_size_name(i),
           (float)b_stat->all_read_io[i] / b_stat->all_read_cnt);
  }
  printf("\n");

  printf(HEADER3 "\t [write] \t" RESET);
  printf("total bio: %llu, ", b_stat->all_write_cnt);
  printf("avg lat(us)");
  for(i = 0; i < WRITE_SIZE_NUM; i++) {
    printf(", [%s: %.2f]", write_size_name(i), (float) b_stat->all_write_time[i] / 1000 / b_stat->all_write_io[i]);
  }
  printf("\n");
  
  printf(HEADER3 "\t [write dist] \t" RESET);
  for (i = 0; i < WRITE_SIZE_NUM; i++) {
    printf(" [%s: %.2f]", write_size_name(i),
           (float)b_stat->all_write_io[i] / b_stat->all_write_cnt);
  }
  printf("\n");

  fflush(stdout);
}

void print_blk_layer_stat() {
  if (blk_stat) {
    print_blk_stat(blk_stat);
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

void init_blk_layer_monitor() {
  map_nttm_blk_data();
}

void exit_blk_layer_monitor() {
  unmap_nttm_blk_data();
}