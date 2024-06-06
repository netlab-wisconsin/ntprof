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

#include "nvmetcp_monitor_user.h"

#define BUFFER_SIZE PAGE_SIZE

static volatile int keep_running = 1;

/** filters */
static char device_name[32] = "";

void int_handler(int dummy) { keep_running = 0; }

void arg_device_name(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      if (i + 1 < argc) {
        /** TODO: make sure the device name exists*/
        strncpy(device_name, argv[i + 1], sizeof(device_name));
        break;
      }
    }
  }
}

void print(struct blk_stat *tr_data, bool clear, char* header ) {
  if(clear)
    printf("\033[H\033[J");
  printf("header: %s\n", header);
  printf("device_name: %s\n", device_name);
  pr_blk_tr(tr_data);
}

int main(int argc, char **argv) {
  arg_device_name(argc, argv);

  int control_fd, raw_blk_stat_fd, param_fd, sample_10s_fd, sample_2s_fd;
  struct blk_stat *raw_blk_stat;
  struct blk_stat *blk_stat_10s;
  struct blk_stat *blk_stat_2s;

  signal(SIGINT, int_handler);

  /** map the raw_blk_stat*/
  raw_blk_stat_fd = open("/proc/ntm_raw_blk_stat", O_RDONLY);
  if (raw_blk_stat_fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  raw_blk_stat =
      mmap(NULL, sizeof(struct blk_stat), PROT_READ, MAP_SHARED, raw_blk_stat_fd, 0);
  if (raw_blk_stat == MAP_FAILED) {
    perror("mmap");
    close(raw_blk_stat_fd);
    exit(EXIT_FAILURE);
  }

  /** map the blk_stat_10s and blk_stat_2s */
  sample_10s_fd = open("/proc/ntm_sample_10s", O_RDONLY);
  if (sample_10s_fd == -1) {
    perror("open");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    close(sample_10s_fd);
    exit(EXIT_FAILURE);
  }

  blk_stat_10s =
      mmap(NULL, sizeof(struct blk_stat), PROT_READ, MAP_SHARED, sample_10s_fd, 0);
  if (blk_stat_10s == MAP_FAILED) {
    perror("mmap");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    close(sample_10s_fd);
    exit(EXIT_FAILURE);
  }

  sample_2s_fd = open("/proc/ntm_sample_2s", O_RDONLY);
  if (sample_2s_fd == -1) {
    perror("open");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    munmap(blk_stat_10s, sizeof(struct blk_stat));
    close(sample_2s_fd);
    exit(EXIT_FAILURE);
  }

  blk_stat_2s =
      mmap(NULL, sizeof(struct blk_stat), PROT_READ, MAP_SHARED, sample_2s_fd, 0);
  if (blk_stat_2s == MAP_FAILED) {
    perror("mmap");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    munmap(blk_stat_10s, sizeof(struct blk_stat));
    close(sample_2s_fd);
    exit(EXIT_FAILURE);
  }

  /** map parameters*/
  param_fd = open("/proc/ntm_params", O_WRONLY);
  if (param_fd == -1) {
    perror("open");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    close(raw_blk_stat_fd);
    exit(EXIT_FAILURE);
  }
  write(param_fd, device_name, strlen(device_name));
  close(param_fd);

  /** send control command = 1, start recording */
  control_fd = open("/proc/ntm_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    close(control_fd);
    exit(EXIT_FAILURE);
  }
  write(control_fd, "1", 1);
  close(control_fd);

  while (keep_running) {
    print(raw_blk_stat, true, "RAW BLK STAT");
    print(blk_stat_10s, false, "LAST 10s");
    print(blk_stat_2s, false, "LAST 2s");
    sleep(1);
  }

  /** send control command = 0, stop recording */
  control_fd = open("/proc/ntm_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    munmap(raw_blk_stat, sizeof(struct blk_stat));
    munmap(blk_stat_10s, sizeof(struct blk_stat));
    munmap(blk_stat_2s, sizeof(struct blk_stat));
    close(raw_blk_stat_fd);
    close(sample_10s_fd);
    close(sample_2s_fd);
    exit(EXIT_FAILURE);
  }
  write(control_fd, "0", 1);
  close(control_fd);

  // FILE *data_file = fopen("nvmetcp_monitor.data", "w");
  // if (!data_file) {
  //     perror("fopen");
  //     munmap(tr_data, sizeof(struct blk_tr));
  //     close(mmap_fd);
  //     exit(EXIT_FAILURE);
  // }
  // serialize_blk_tr(tr_data, data_file);
  // fclose(data_file);

  printf("Data saved to nvmetcp_monitor.data\n");

  munmap(raw_blk_stat, sizeof(struct blk_stat));
  close(raw_blk_stat_fd);

  return 0;
}