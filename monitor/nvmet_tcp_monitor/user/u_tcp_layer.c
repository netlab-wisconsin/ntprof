#include "u_tcp_layer.h"

#include <stdio.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "nttm_com.h"
#include "output.h"

struct tcp_stat *tcp_s;

void print_tcp_stat(struct tcp_stat *stat) {
  printf(HEADER1 "[TCP LAYER]:" RESET "\n");
  for (int i = 0; i < MAX_QID; i++) {
    if (stat->sks[i].cwnd == 0) continue;
    /** print qid, port, cwnd, pkt-in-flight in one line */
    printf("\tqid: %d, cwnd: %d, pkt-in-flight: %d, last event: %s\n", i,
           stat->sks[i].cwnd, stat->sks[i].pkt_in_flight, stat->sks[i].last_event);
  }
}

void print_tcp_layer_stat() {
  if (tcp_s) {
    print_tcp_stat(tcp_s);
  }
}

void map_nttm_tcp_data() {
  int fd;

  fd = open("/proc/nttm/tcp/stat", O_RDONLY);
  if (fd == -1) {
    printf("Failed to open /proc/nttm/tcp/stat\n");
  }
  tcp_s = (struct tcp_stat *)mmap(NULL, sizeof(*tcp_s), PROT_READ,
                                     MAP_SHARED, fd, 0);
  if (tcp_s == MAP_FAILED) {
    printf("Failed to mmap /proc/nttm/tcp/stat\n");
  }
  close(fd);
}

void unmap_nttm_tcp_data(){
  if (tcp_s) {
    munmap(tcp_s, sizeof(struct tcp_stat));
  }
}

void init_tcp_layer_monitor() {
  map_nttm_tcp_data();
}

void exit_tcp_layer_monitor() {
  unmap_nttm_tcp_data();
}
