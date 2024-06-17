#include "u_blk_layer.h"
#include "u_ntm.h"

/** 
 * print a blk_stat data structure
 * @param b_stat the blk_stat data structure
 * @param header the header of the print
*/
void print_blk_stat(struct blk_stat *b_stat, char *header) {
  printf("header: %s \t", header);
  printf("device_name: %s\n", args->dev);
  char *dis_header[9] = {"<4KB", "4KB",   "8KB",    "16KB",  "32KB",
                         "64KB", "128KB", ">128KB", "others"};
  printf("read total: %llu\n", b_stat->read_count);
  printf("read cnt");
  int i;
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %llu]", dis_header[i], b_stat->read_io[i]);
  }
  printf("\nread dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->read_io[i] / b_stat->read_count);
  }
  printf("\nwrite total: %llu\n", b_stat->write_count);
  printf("write cnt");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %llu]", dis_header[i], b_stat->write_io[i]);
  }
  printf("\nwrite dis");
  for (i = 0; i < 9; i++) {
    printf(", \t[%s: %.2f]", dis_header[i],
           (float)b_stat->write_io[i] / b_stat->write_count);
  }
  printf("\n\n");
}

/**
 * print the blk_stat_set
 * - print the raw_blk_stat
 * - print the blk_stat
 * @param bs the blk_stat_set
 * @param clear clear the screen or not
*/
void print_blk_stat_set() {
  print_blk_stat(blk_set.raw_blk_stat, "RAW BLK STAT");
  /** 
   * TODO: make it a global variable, 
   * instead of calling it repeatedly
  */
  char header[32];
  sprintf(header, "LAST %ds", args->win); 
  print_blk_stat(blk_set.blk_stat, header);
}


/**
 * set device name and blk_stat_set ptr
*/
void init_ntm_blk() {
  
}


/**
 * map the blk layer statistic data structures
*/
void map_ntm_blk_data() {
  int  raw_blk_stat_fd, sample_fd;

  /** map the raw_blk_stat*/
  raw_blk_stat_fd = open("/proc/ntm/blk/ntm_raw_blk_stat", O_RDONLY);
  if (raw_blk_stat_fd == -1) {
    printf("Failed to open /proc/ntm/blk/ntm_raw_blk_stat\n");
    exit(EXIT_FAILURE);
  }
  blk_set.raw_blk_stat = mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                               MAP_SHARED, raw_blk_stat_fd, 0);
  if (blk_set.raw_blk_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/blk/ntm_raw_blk_stat\n");
    close(raw_blk_stat_fd);
    exit(EXIT_FAILURE);
  }
  close(raw_blk_stat_fd);

  /** map the blk_stat */
  sample_fd = open("/proc/ntm/blk/ntm_sample", O_RDONLY);
  if (sample_fd == -1) {
    printf("Failed to open /proc/ntm/blk/ntm_sample\n");
    munmap(blk_set.raw_blk_stat, sizeof(struct blk_stat));
    close(sample_fd);
    exit(EXIT_FAILURE);
  }
  blk_set.blk_stat = mmap(NULL, sizeof(struct blk_stat), PROT_READ,
                               MAP_SHARED, sample_fd, 0);
  if (blk_set.blk_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/blk/ntm_sample\n");
    munmap(blk_set.raw_blk_stat, sizeof(struct blk_stat));
    close(sample_fd);
    exit(EXIT_FAILURE);
  }
  close(sample_fd);
}

/**
 * unmap the blk layer statistic data structures
*/
void unmap_ntm_blk_data() {
  munmap(blk_set.raw_blk_stat, sizeof(struct blk_stat));
  munmap(blk_set.blk_stat, sizeof(struct blk_stat));
}