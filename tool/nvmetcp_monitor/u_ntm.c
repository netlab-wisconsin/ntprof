
#include "u_ntm.h"

#include "u_blk_layer.h"
#include "u_nvmetcp_layer.h"

static volatile int keep_running = 1;

// char device_name[32];

Arguments args;

void int_handler(int dummy) { keep_running = 0; }

/** a set of blk layer statistics, each element is mapped with a /proc file */
struct blk_stat_set blk_stat_set;

struct nvmetcp_stat_set nvmetcp_stat_set;

void print_usage() {
  printf("Usage: ntm track [options]\n");
  printf("Options:\n");
  printf("  -dev=<device>       Device name (default: all devices)\n");
  printf("  -rate=<rate>        IO sample rate (default: 0.001)\n");
  printf("  -type=<type>        Request type (read or write, default: both)\n");
  printf(
      "  -win=<window>       Sliding window width in seconds (default: 10)\n");
  printf("  -size=<size>        IO size in bytes (default: all sizes)\n");
  printf("  -qid=<queue_id>     Queue ID (default: all queues)\n");
  printf(
      "  -nrate=<nrate>      Network packet sample rate (default: 0.00001)\n");
}

void parse_arguments(int argc, char *argv[], Arguments *args) {
  strcpy(args->dev, "all devices");
  args->rate = 0.001;
  args->io_type = _BOTH;
  args->win = 10;
  args->size = 0;
  args->qid = -1;
  args->nrate = 0.00001;

  for (int i = 2; i < argc; i++) {
    if (strncmp(argv[i], "-dev=", 5) == 0) {
      strcpy(args->dev, argv[i] + 5);
    } else if (strncmp(argv[i], "-rate=", 6) == 0) {
      args->rate = atof(argv[i] + 6);
    } else if (strncmp(argv[i], "-type=", 6) == 0) {
      char* type = argv[i] + 6;
      /** if upper case, conver to lower case */
      if (type[0] >= 'A' && type[0] <= 'Z') {
        type[0] += 32;
      }
      if(strcmp(type, "read") == 0) {
        args->io_type = _READ;
      } else if(strcmp(type, "write") == 0) {
        args->io_type = _WRITE;
      } else if(strcmp(type, "both") == 0) {
        args->io_type = _BOTH;
      } else {
        printf("Invalid type: %s\n", type);
        print_usage();
        exit(EXIT_FAILURE);
      }
    } else if (strncmp(argv[i], "-win=", 5) == 0) {
      args->win = atoi(argv[i] + 5);
    } else if (strncmp(argv[i], "-size=", 6) == 0) {
      args->size = atoi(argv[i] + 6);
    } else if (strncmp(argv[i], "-qid=", 5) == 0) {
      args->qid = atoi(argv[i] + 5);
    } else if (strncmp(argv[i], "-nrate=", 7) == 0) {
      args->nrate = atof(argv[i] + 7);
    } else {
      printf("Invalid argument: %s\n", argv[i]);
      print_usage();
      exit(EXIT_FAILURE);
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
  write(param_fd, args.dev, strlen(args.dev));
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
  if (argc < 2 || strcmp(argv[1], "track") != 0) {
    if(argc<2){
      printf("argc < 2\n");
    } else {
      printf("argv[1]: %s\n", argv[1]);
    }
    print_usage();
    return EXIT_FAILURE;
  }

  signal(SIGINT, int_handler);

  parse_arguments(argc, argv, &args);
  
  /** send msg to kernel space to start recording */
  start_ntm();

  init_ntm_blk(&blk_stat_set);
  map_ntm_blk_data();

  init_ntm_nvmetcp(&nvmetcp_stat_set);
  map_ntm_nvmetcp_data();

  while (keep_running) {
    print_blk_stat_set(&blk_stat_set, true);
    print_nvmetcp_stat_set(&nvmetcp_stat_set, false);
    sleep(1);
  }

  unmap_ntm_blk_data();
  unmap_ntm_nvmetcp_data();

  printf("start exit ntm_user\n");

  exit_ntm();
  return 0;
}