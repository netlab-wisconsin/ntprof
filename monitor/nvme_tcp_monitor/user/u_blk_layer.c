#include "u_blk_layer.h"

#include "output.h"
#include "u_ntm.h"

void print_shared_blk_stat(struct shared_blk_layer_stat *shared) {
  printf(HEADER1 "[BLOCK LAYER]" RESET "\n");
  printf("device_name: %s\n", args->dev);
  char *dis_header[9] = {"<4KB", "4KB",   "8KB",    "16KB",  "32KB",
                         "64KB", "128KB", ">128KB", "others"};
  printf(HEADER2 "all time:\n" RESET);
  printf(HEADER3 "[read] \t" RESET);
  printf("total bio: %llu, ", shared->all_time_stat.read_count);
  printf("avg lat(us): %.2f\n", (float)shared->all_time_stat.read_lat / 1000 /
                                    shared->all_time_stat.read_count);

  printf(HEADER3 "[read dist] \t" RESET);
  int i;
  for (i = 0; i < 9; i++) {
    printf(" [%s: %.2f]", dis_header[i],
           (float)shared->all_time_stat.read_io[i] /
               shared->all_time_stat.read_count);
  }
  printf("\n");

  printf(HEADER3 "[read lat] \t" RESET);
  for (i = 0; i < 9; i++) {
    printf(" [%s: %.2f]", dis_header[i],
           (float)shared->all_time_stat.read_io_lat[i] /
               shared->all_time_stat.read_count);
  }
  printf("\n");

  printf(HEADER3 "[write] \t" RESET);
  printf("total bio: %llu, ", shared->all_time_stat.write_count);
  printf("avg lat(us): %.2f\n", (float)shared->all_time_stat.write_lat / 1000 /
                                    shared->all_time_stat.write_count);

  printf(HEADER3 "[write dist] \t" RESET);
  for (i = 0; i < 9; i++) {
    printf(" [%s: %.2f]", dis_header[i],
           (float)shared->all_time_stat.write_io[i] /
               shared->all_time_stat.write_count);
  }
  printf("\n");

  printf(HEADER3 "[write lat] \t" RESET);
  for (i = 0; i < 9; i++) {
    printf(" [%s: %.2f]", dis_header[i],
           (float)shared->all_time_stat.write_io_lat[i] /
               shared->all_time_stat.write_count);
  }
  printf("\n");

  fflush(stdout);
}

void blk_layer_monitor_display() { print_shared_blk_stat(shared); }

/**
 * map the blk layer statistic data structures
 */
void map_ntm_blk_data() {
  int fd;

  /** map the raw_blk_stat*/
  fd = open("/proc/ntm/blk/stat", O_RDONLY);
  if (fd == -1) {
    printf("Failed to open /proc/ntm/blk/stat\n");
    exit(EXIT_FAILURE);
  }
  shared = mmap(NULL, sizeof(struct shared_blk_layer_stat), PROT_READ,
                MAP_SHARED, fd, 0);
  if (shared == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/blk/stat\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
  close(fd);
}

/**
 * unmap the blk layer statistic data structures
 */
void unmap_ntm_blk_data() {
  munmap(shared, sizeof(struct shared_blk_layer_stat));
}

/**
 * set device name and blk_stat_set ptr
 */
void init_blk_layer_monitor() { map_ntm_blk_data(); }

void exit_blk_layer_monitor() { unmap_ntm_blk_data(); }