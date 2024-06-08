
#include "ntm_user.h"
#include "blk_layer_user.h"

#define BUFFER_SIZE PAGE_SIZE


/** filters */
static char device_name[32] = "";

/** a set of blk layer statistics, each element is mapped with a /proc file */
struct blk_stat_set blk_stat_set;

/**
 * read cmd line parameter -d device_name
 * @param argc the number of arguments
 * @param argv the arguments
*/
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


/** 
 * send message to the kernel to start recording
 * - write device name to /proc/ntm/ntm_params
 * - write 1 to /proc/ntm/ntm_ctrl
*/
void start_ntm() {
  int param_fd, control_fd;

  /** write parameter name to the kernel */
  param_fd = open("/proc/ntm/ntm_params", O_WRONLY);
  if (param_fd == -1) {
    perror("open");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    exit(EXIT_FAILURE);
  }
  write(param_fd, device_name, strlen(device_name));
  close(param_fd);

  /** send control command = 1, start recording */
  control_fd = open("/proc/ntm/ntm_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    munmap(blk_set->raw_blk_stat, sizeof(struct blk_stat));
    close(control_fd);
    exit(EXIT_FAILURE);
  }
  write(control_fd, "1", 1);
  close(control_fd);
}

void exit_ntm() {
  /** send control command = 0, stop recording */
  int control_fd;
  control_fd = open("/proc/ntm/ntm_ctrl", O_WRONLY);
  if (control_fd == -1) {
    perror("open");
    close(control_fd);
    exit(EXIT_FAILURE);
  }
  write(control_fd, "0", 1);
}

int main(int argc, char **argv) {
  /** read cmdline parameter - device name */
  arg_device_name(argc, argv);

  signal(SIGINT, int_handler);

  /** send msg to kernel space to start recording */
  start_ntm();

  init_ntm_blk(device_name, &blk_stat_set);

  map_ntm_blk_data();

  while (keep_running) {
    print_blk_stat_set(&blk_stat_set, true);
    sleep(1);
  }

  unmap_ntm_blk_data();

  exit_ntm();
  return 0;
}