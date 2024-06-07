
#include "ntm_user.h"
#include "blk_layer_user.h"

#define BUFFER_SIZE PAGE_SIZE


/** filters */
static char device_name[32] = "";

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

struct blk_stat_set blk_stat_set;

void init_ntm() {
  int param_fd, control_fd;

  signal(SIGINT, int_handler);
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

  init_ntm();

  init_ntm_blk(device_name, &blk_stat_set);

  start_ntm_blk();

  while (keep_running) {
    print_blk_stat_set(&blk_stat_set, true);
    sleep(1);
  }

  exit_ntm_blk();

  exit_ntm();
  return 0;
}