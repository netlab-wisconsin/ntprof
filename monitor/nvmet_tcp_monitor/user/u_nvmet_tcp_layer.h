#ifndef U_NVMET_TCP_LAYER_H
#define U_NVMET_TCP_LAYER_H

#include <stdio.h>

#include "nttm_com.h"
#include "output.h"

static struct nvmet_tcp_stat *nvmet_tcp_stat;

/**
 *
 * struct nvmet_tcp_read_breakdown {
  u64 in_nvmet_tcp_time;
  u64 in_blk_time;
  u64 end2end_time;
  u32 cnt;
};

struct nvmet_tcp_write_breakdown {
  u64 make_r2t_time;
  u64 in_nvmet_tcp_time;
  u64 in_blk_time;
  u64 end2end_time;
  u32 cnt;
};

 */
void print_read_breakdown(struct nvmet_tcp_read_breakdown *ns) {
  printf("\t read breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %lu, ", ns->cnt);
    printf("in_nvmet_tcp_time(us): %.6f,",
           (float)ns->in_nvmet_tcp_time / 1000 / ns->cnt);
    printf("in_blk_time(us): %.6f,", (float)ns->in_blk_time / 1000 / ns->cnt);
    printf("end2end_time(us): %.6f\n",
           (float)ns->end2end_time / 1000 / ns->cnt);
  } else {
    printf("cnt: %lu\n", ns->cnt);
  }
}

void print_write_breakdown(struct nvmet_tcp_write_breakdown *ns) {
  printf("\t write breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %lu\t", ns->cnt);
    printf("make_r2t_time(us): %.6f, ",
           (float)ns->make_r2t_time / 1000 / ns->cnt);
    printf("in_nvmet_tcp_time(us): %.6f, ",
           (float)ns->in_nvmet_tcp_time / 1000 / ns->cnt);
    printf("in_blk_time(us): %.6f, ", (float)ns->in_blk_time / 1000 / ns->cnt);
    printf("end2end_time(us): %.6f\n",
           (float)ns->end2end_time / 1000 / ns->cnt);
  } else {
    printf("cnt: %lu\n", ns->cnt);
  }
}

void print_nvmet_tcp_layer_stat() {
  if (nvmet_tcp_stat) {
    printf(HEADER1 "[NVMET_TCP LAYER]:\n" RESET);
    printf(HEADER2 "all time lat(us)" RESET "\n");
    int i;
    for(i = 0; i < 9; i++) {
      printf(HEADER3 "size=%s, \n" RESET, size_name(i));
      print_read_breakdown(&nvmet_tcp_stat->all_read[i]);
      print_write_breakdown(&nvmet_tcp_stat->all_write[i]);
    }
  } else {
    printf("nvmet_tcp_stat is NULL\n");
  }
}

void map_nttm_nvmet_tcp_data() {
  int fd;

  /** map the raw_nvmetcp_stat */
  fd = open("/proc/nttm/nvmet_tcp/stat", O_RDONLY);
  if (fd == -1) {
    printf("Failed to open /proc/nttm/nvmet_tcp/stat\n");
  }
  nvmet_tcp_stat = (struct nvmet_tcp_stat *)mmap(
      NULL, sizeof(struct nvmet_tcp_stat), PROT_READ, MAP_SHARED, fd, 0);
  if (nvmet_tcp_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/nttm/nvmet_tcp/stat\n");
  }
  close(fd);
}

void unmap_nttm_nvmet_tcp_data() {
  if (nvmet_tcp_stat) munmap(nvmet_tcp_stat, sizeof(struct nvmet_tcp_stat));
}

#endif