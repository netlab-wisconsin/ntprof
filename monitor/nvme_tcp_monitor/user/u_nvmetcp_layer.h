#ifndef U_NVMETCP_LAYER_H
#define U_NVMETCP_LAYER_H

#include "ntm_com.h"

static struct nvme_tcp_stat *nvme_tcp_stat;

void print_blk_lat_stat(struct blk_lat_stat *ns) {
  printf("blk lat: \t");
  if (ns->cnt > 0)
    printf("average(us): %.6f\n",
           (float)ns->sum_blk_layer_lat / ns->cnt / 1000);
  else
    printf("average(us): cnt=0\n");
}

void print_read_breakdown(struct nvmetcp_read_breakdown *ns) {
  printf("read breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %d\t", ns->cnt);
    printf("t_inqueue(us): %.6f\t", (float)ns->t_inqueue / 1000 / ns->cnt);
    printf("t_reqcopy(us): %.6f\t", (float)ns->t_reqcopy / 1000 / ns->cnt);
    printf("t_datacopy(us): %.6f\t", (float)ns->t_datacopy / 1000 / ns->cnt);
    printf("t_waiting(us): %.6f\t", (float)ns->t_waiting / 1000 / ns->cnt);
    printf("t_endtoend(us): %.6f\n", (float)ns->t_endtoend / 1000 / ns->cnt);
    printf("t_waitproc(us): %.6f\n", (float)ns->t_waitproc / 1000 / ns->cnt);
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_write_breakdown(struct nvmetcp_write_breakdown *ns) {
  printf("write breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %d\t", ns->cnt);
    printf("t_inqueue(us): %.6f\t", (float)ns->t_inqueue / 1000 / ns->cnt);
    printf("t_reqcopy(us): %.6f\t", (float)ns->t_reqcopy / 1000 / ns->cnt);
    printf("t_datacopy(us): %.6f\t", (float)ns->t_datacopy / 1000 / ns->cnt);
    printf("t_waiting(us): %.6f\t", (float)ns->t_waiting / 1000 / ns->cnt);
    printf("t_endtoend(us): %.6f\n", (float)ns->t_endtoend / 1000 / ns->cnt);
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_nvme_tcp_stat() {
  if (nvme_tcp_stat) {
    print_read_breakdown(&nvme_tcp_stat->read);
    print_write_breakdown(&nvme_tcp_stat->write);
    print_blk_lat_stat(&nvme_tcp_stat->blk_lat);
  } else {
    printf("nvme_tcp_stat is NULL\n");
  }
}

void map_ntm_nvmetcp_data() {
  int fd;

  /** map the raw_nvmetcp_stat */
  fd = open("/proc/ntm/nvmetcp/stat", O_RDONLY);
  if (fd == -1) {
    printf("Failed to open /proc/ntm/nvmetcp/stat\n");
    exit(EXIT_FAILURE);
  }
  nvme_tcp_stat =
      mmap(NULL, sizeof(struct nvme_tcp_stat), PROT_READ, MAP_SHARED, fd, 0);

  if (nvme_tcp_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/nvmetcp/stat\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
  close(fd);
}

void unmap_ntm_nvmetcp_data() {
  printf("unmmap_raw_nvmetcp_stat\n");

  if (nvme_tcp_stat) {
    munmap(nvme_tcp_stat, sizeof(struct nvme_tcp_stat));
  }
}

#endif  // U_NVMETCP_LAYER_H