#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nvmetcp_monitor.h"

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

void print(struct blk_tr *tr_data) {
  printf("\033[H\033[J");
  printf("device_name: %s\n", device_name);
  pr_blk_tr(tr_data);
}

int main(int argc, char **argv) {
  // read the parameter
  // there are named argment, for example, the user will input -d nvme0n1
  // the parameter will be stored in device_name
  // the device_name is used to filter the device name
  // if the device name is not the same as the device name in the request, the
  // request will be ignored

  // start dealing with the parameter

  arg_device_name(argc, argv);

  // pass the device name to the kernel module

  int control_fd, mmap_fd, param_fd;
  struct blk_tr *tr_data;

  signal(SIGINT, int_handler);

  mmap_fd = open("/proc/nvmetcp_monitor_data", O_RDONLY);
  if (mmap_fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  tr_data =
      mmap(NULL, sizeof(struct blk_tr), PROT_READ, MAP_SHARED, mmap_fd, 0);
  if (tr_data == MAP_FAILED) {
    perror("mmap");
    close(mmap_fd);
    exit(EXIT_FAILURE);
  }

  param_fd = open("/proc/nvmetcp_monitor_params", O_WRONLY);
  if (param_fd == -1) {
    perror("open");
    munmap(tr_data, sizeof(struct blk_tr));
    close(mmap_fd);
    exit(EXIT_FAILURE);
  }
  write(param_fd, device_name, strlen(device_name));
  close(param_fd);

  control_fd = open("/proc/nvmetcp_monitor_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    munmap(tr_data, sizeof(struct blk_tr));
    close(mmap_fd);
    exit(EXIT_FAILURE);
  }
  write(control_fd, "1", 1);
  close(control_fd);

  while (keep_running) {
    print(tr_data);
    sleep(1);
  }

  control_fd = open("/proc/nvmetcp_monitor_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    munmap(tr_data, sizeof(struct blk_tr));
    close(mmap_fd);
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

  munmap(tr_data, sizeof(struct blk_tr));
  close(mmap_fd);

  return 0;
}