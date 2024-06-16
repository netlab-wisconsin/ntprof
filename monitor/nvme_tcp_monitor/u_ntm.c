
#include "u_ntm.h"

#include "u_blk_layer.h"
#include "u_nvmetcp_layer.h"

static volatile int keep_running = 1;

// char device_name[32];

Arguments* args;

/**
 * print the arguments in one line, separate different args with '\t'
*/
void print_args(Arguments *args) {
    printf("dev: %s\t", args->dev);
    printf("rate: %.3f\t", args->rate);
    char io_type_str[32];
    switch (args->io_type) {
        case _READ:
            strcpy(io_type_str, "READ");
            break;
        case _WRITE:
            strcpy(io_type_str, "WRITE");
            break;
        case _BOTH:
            strcpy(io_type_str, "ANY");
            break;
        default:
            strcpy(io_type_str, "UNK");
            break;
    }
    printf("io_type: %s\t", io_type_str);
    printf("win: %d\t", args->win);
    char io_size_str[32];
    if (args->io_size == -1) {
        strcpy(io_size_str, "ANY");
    } else {
        sprintf(io_size_str, "%d", args->io_size);
    }
    printf("io_size: %s\t", io_size_str);
    char qid_str[32];
    if (args->qid == -1) {
        strcpy(qid_str, "ANY");
    } else {
        sprintf(qid_str, "%d", args->qid);
    }
    printf("qid: %s\t", qid_str);
    printf("nrate: %.5f\n", args->nrate);
}

void int_handler(int dummy) { keep_running = 0; }

/** a set of blk layer statistics, each element is mapped with a /proc file */
struct blk_stat_set blk_stat_set;



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
  /** io_size = -1 indicates all io_size are considered */
  args->io_size = -1;
  /** qid == -1, indicates all queues are considered */
  args->qid = -1;
  args->nrate = 0.00001;

  for (int i = 2; i < argc; i++) {
    if (strncmp(argv[i], "-dev=", 5) == 0) {
      strcpy(args->dev, argv[i] + 5);
    } else if (strncmp(argv[i], "-rate=", 6) == 0) {
      args->rate = atof(argv[i] + 6);
    } else if (strncmp(argv[i], "-type=", 6) == 0) {
      char *type = argv[i] + 6;
      /** if upper case, conver to lower case */
      if (type[0] >= 'A' && type[0] <= 'Z') {
        type[0] += 32;
      }
      if (strcmp(type, "read") == 0) {
        args->io_type = _READ;
      } else if (strcmp(type, "write") == 0) {
        args->io_type = _WRITE;
      } else if (strcmp(type, "both") == 0) {
        args->io_type = _BOTH;
      } else {
        printf("Invalid type: %s\n", type);
        print_usage();
        exit(EXIT_FAILURE);
      }
    } else if (strncmp(argv[i], "-win=", 5) == 0) {
      args->win = atoi(argv[i] + 5);
    } else if (strncmp(argv[i], "-size=", 6) == 0) {
      args->io_size = atoi(argv[i] + 6);
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
 * map variables with the kernel space
 * - args
*/
void map_varialbes(){
  char *fname;
  int fd;

  /** map the args variables */
  fd = open("/proc/ntm/args", O_RDWR);
  if (fd == -1) {
    goto fail_open;
  }
  args = mmap(NULL, sizeof(Arguments), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(args == MAP_FAILED) {
    goto fail_open;
  }
  close(fd);
  return;

fail_open:
  close(fd);
  fprintf(stderr, "Failed to open %s\n", fname);
  perror("open");
  exit(EXIT_FAILURE);
}

/**
 * send message to the kernel to start recording
 * - write 1 to /proc/ntm/ntm_ctrl
 */
void start_ntm() {
  char *fname;
  int fd;

  /** send control command = 1, start recording */
  fname = "/proc/ntm/ctrl";
  fd = open("/proc/ntm/ctrl", O_WRONLY);
  if (fd == -1) {
    goto fail;
  }
  write(fd, "1", 1);
  close(fd);

return;

fail_open:
  close(fd);
  fprintf(stderr, "Failed to open %s\n", fname);
  perror("open");
fail:
  exit(EXIT_FAILURE);
}

void exit_ntm() {
  /** send control command = 0, stop recording */
  int fd;
  char *fname;

  /** unmap the args variables */
  munmap(args, sizeof(Arguments));

  /** write 0 to ctrl to stop recording */
  fname = "/proc/ntm/ctrl";
  fd = open("/proc/ntm/ctrl", O_WRONLY);
  if (fd == -1) goto fail_open;
  write(fd, "0", 1);
  return;

fail_open:
  close(fd);
  fprintf(stderr, "Failed to open %s\n", fname);
  perror("open");
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  /** read cmdline parameter - device name */
  if (argc < 2 || strcmp(argv[1], "track") != 0) {
    if (argc < 2) {
      printf("argc < 2\n");
    } else {
      printf("argv[1]: %s\n", argv[1]);
    }
    print_usage();
    return EXIT_FAILURE;
  }

  signal(SIGINT, int_handler);

  printf("debug: start map_varialbes\n");
  map_varialbes();
  printf("debug: after map_varialbes\n");
  parse_arguments(argc, argv, args);

  /** send msg to kernel space to start recording */
  start_ntm();

  init_ntm_blk(&blk_stat_set);

  map_ntm_blk_data();

  map_ntm_nvmetcp_data();

  while (keep_running) {
    printf("\033[H\033[J");
    print_args(args);
    print_blk_stat_set(&blk_stat_set, false);
    print_nvme_tcp_stat();
    sleep(1);
  }

  unmap_ntm_blk_data();
  unmap_ntm_nvmetcp_data();

  printf("start exit ntm_user\n");

  exit_ntm();
  return 0;
}