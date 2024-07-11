#include "u_nvmet_tcp_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nttm_com.h"
#include "output.h"

struct nvmet_tcp_stat *nvmet_tcp_stat;

/**
 *
 * struct nvmet_tcp_read_breakdown {
  u64 nvmet_tcp_processing;
  u64 nvme_submission_and_execution;
  u64 end2end_time;
  u32 cnt;
};

struct nvmet_tcp_write_breakdown {
  u64 make_r2t_time;
  u64 nvmet_tcp_processing;
  u64 nvme_submission_and_execution;
  u64 end2end_time;
  u32 cnt;
};

 */
void print_read_breakdown(struct nvmet_tcp_read_breakdown *ns) {
  printf("\t read breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %lu, ", ns->cnt);
  //     unsigned long long cmd_capsule_queueing;
  // unsigned long long cmd_processing;
  // unsigned long long submission_and_execution;
  // unsigned long long comp_queueing;
  // unsigned long long resp_processing;
  // unsigned long long end2end_time;
  // unsigned long cnt;

    printf("cmd_caps_q(us): %.2f, ",
           (float)ns->cmd_caps_q / 1000 / ns->cnt);
    printf("cmd_proc(us): %.2f, ",
            (float)ns->cmd_proc / 1000 / ns->cnt);
    printf("sub_and_exec(us): %.2f, ",
            (float)ns->sub_and_exec / 1000 / ns->cnt);
    printf("comp_q(us): %.2f, ",
            (float)ns->comp_q / 1000 / ns->cnt);
    printf("resp_proc(us): %.2f, ",
            (float)ns->resp_proc / 1000 / ns->cnt);
    printf("end2end(us): %.2f\n",
            (float)ns->end2end / 1000 / ns->cnt);
  } else {
    printf("cnt: %lu\n", ns->cnt);
  }
}

void print_write_breakdown(struct nvmet_tcp_write_breakdown *ns) {
  printf("\t write breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %lu\t", ns->cnt);
    printf("make_r2t_time(us): %.2f, ",
           (float)ns->make_r2t_time / 1000 / ns->cnt);
    printf("nvmet_tcp_processing(us): %.2f, ",
           (float)ns->nvmet_tcp_processing / 1000 / ns->cnt);
    printf("nvme_submission_and_execution(us): %.2f, ",
           (float)ns->nvme_submission_and_execution / 1000 / ns->cnt);
    printf("end2end_time(us): %.2f\n",
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
    for (i = 0; i < SIZE_NUM; i++) {
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

void init_nvmet_tcp_layer_monitor() { map_nttm_nvmet_tcp_data(); }

void exit_nvmet_tcp_layer_monitor() { unmap_nttm_nvmet_tcp_data(); }