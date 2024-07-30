
#include "u_ntm.h"

#include <ctype.h>

#include "u_blk_layer.h"
#include "u_nvme_tcp_layer.h"
#include "u_tcp_layer.h"

static volatile int keep_running = 1;

// char device_name[32];

Arguments *args;

bool is_number(const char *str) {
  while (*str) {
    if (!isdigit(*str++)) {
      return false;
    }
  }
  return true;
}

/**
 * print the arguments in one line, separate different args with '\t'
 */
void print_args(Arguments *args) {
  printf("dev: %s\t", args->dev);
  printf("rate: %d\t", args->rate);
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
  printf("qid: %s\t", args->qstr);
  /** only for debug */
  // for (int i = 0; i < MAX_QID; i++) {
  //   printf("%d, %d \t", i, args->qid[i]);
  // }
  // printf("\n");
  printf("nrate: %d\n", args->nrate);
}

void int_handler(int dummy) { keep_running = 0; }

void print_usage() {
  printf("Usage: nttm track [options]\n");
  printf("Options:\n");
  printf("  -dev=<device>       Device name (default: all devices)\n");
  printf("  -rate=<rate>        IO sample rate (default: 0.001)\n");
  printf("  -type=<type>        Request type (read or write, default: both)\n");
  printf("  -size=<size>        IO size in bytes (default: all sizes)\n");
  printf("  -qid=<queue_id>     Queue ID (default: all queues)\n");
  // printf(
  //     "  -nrate=<nrate>      Network packet sample rate (default: 0.00001)\n");
  // printf("  -detail=<print>     Print detail or not (default: false)");
  // printf(" -batch_thred     Time threshold(ns) for considering as a batch.(default: 1000)\n");
  // printf(" -proc_time     core processing time on initiator for a request.(default: 1000)\n");
}

void parse_qid(const char *qid_str, Arguments *args) {
  memset(args->qid, 0, sizeof(args->qid));
  char *token;
  char *str = strdup(qid_str);
  char *tofree = str;

  /** copy the string */
  strcpy(args->qstr, qid_str);

  while ((token = strsep(&str, ",")) != NULL) {
    if (strchr(token, '-') != NULL) {
      int start, end;
      sscanf(token, "%d-%d", &start, &end);
      for (int i = start; i <= end && i < MAX_QID; i++) {
        args->qid[i] = 1;
      }
    } else if (is_number(token)) {
      int qid = atoi(token);
      if (qid < MAX_QID) {
        args->qid[qid] = 1;
      }
    }
  }
  free(tofree);
}

void parse_arguments(int argc, char *argv[], Arguments *args) {
  /** setting default value */
  strcpy(args->dev, "all devices");
  args->rate = 1000;
  args->io_type = _BOTH;
  args->win = 10;
  args->io_size = -1;
  memset(args->qid, 1, sizeof(args->qid));
  args->qid[0] = 0;
  args->qstr[0] = '\0';
  args->nrate = 100000;
  args->detail = 0;
  // args->latency_group_thred = 1000;

  for (int i = 2; i < argc; i++) {
    if (strncmp(argv[i], "-dev=", 5) == 0) {
      strcpy(args->dev, argv[i] + 5);
    } else if (strncmp(argv[i], "-rate=", 6) == 0) {
      args->rate = atof(argv[i] + 6);
    } else if (strncmp(argv[i], "-type=", 6) == 0) {
      char *type = argv[i] + 6;
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
      parse_qid(argv[i] + 5, args);
    } else if (strncmp(argv[i], "-nrate=", 7) == 0) {
      args->nrate = atof(argv[i] + 7);
    } else if (strncmp(argv[i], "-detail=", 8) == 0) {
      if (strcmp(argv[i] + 8, "true") == 0) {
        args->detail = 1;
      } else if (strcmp(argv[i] + 8, "false") == 0) {
        args->detail = 0;
      } else {
        printf("Invalid detail: %s\n", argv[i] + 8);
        print_usage();
        exit(EXIT_FAILURE);
      }
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
void map_varialbes() {
  char *fname;
  int fd;

  /** map the args variables */
  fd = open("/proc/ntm/args", O_RDWR);
  if (fd == -1) {
    goto fail_open;
  }
  args =
      mmap(NULL, sizeof(Arguments), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (args == MAP_FAILED) {
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

  /** initialize monitors on different layers */
  init_blk_layer_monitor();
  init_nvme_tcp_layer_monitor();
  init_tcp_layer_monitor();


  while (keep_running) {
    printf("\033[H\033[J");
    print_args(args);
    blk_layer_monitor_display();
    nvme_tcp_layer_monitor_display();
    tcp_layer_monitor_display();
    sleep(1);
  }

  /** exit monitors on different layers */
  exit_blk_layer_monitor();
  exit_nvme_tcp_layer_monitor();
  exit_tcp_layer_monitor();

  printf("start exit ntm_user\n");

  exit_ntm();
  return 0;
}