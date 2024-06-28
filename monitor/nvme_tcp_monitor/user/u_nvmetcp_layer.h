#ifndef U_NVMETCP_LAYER_H
#define U_NVMETCP_LAYER_H

#include "ntm_com.h"
#include "output.h"

static struct shared_nvme_tcp_layer_stat *shared_nvme_tcp_stat;

void print_read(struct nvmetcp_read_breakdown *ns, unsigned long long read_before) {
  printf("read breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %d\t", ns->cnt);
    printf("before: %.4f, ", (float)read_before / 1000 / ns->cnt);
    printf("inqueue(us): %.4f, ", (float)ns->t_inqueue / 1000 / ns->cnt);
    printf("reqcopy(us): %.4f, ", (float)ns->t_reqcopy / 1000 / ns->cnt);
    printf("datacopy(us): %.4f, ", (float)ns->t_datacopy / 1000 / ns->cnt);
    printf("waiting(us): %.4f, ", (float)ns->t_waiting / 1000 / ns->cnt);
    printf("waitproc(us): %.4f, ", (float)ns->t_waitproc / 1000 / ns->cnt);
    printf("endtoend(us): %.4f, ", (float)ns->t_endtoend / 1000 / ns->cnt);
    printf("\n");
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_write(struct nvmetcp_write_breakdown *ns, unsigned long long write_before) {
  printf("write breakdown: \t");
  if (ns->cnt) {
    printf("cnt: %d\t", ns->cnt);
    printf("before: %.4f, ", (float)write_before / 1000 / ns->cnt);
    printf("inqueue(us): %.4f, ", (float)ns->t_inqueue / 1000 / ns->cnt);
    printf("reqcopy(us): %.4f, ", (float)ns->t_reqcopy / 1000 / ns->cnt);
    printf("datacopy(us): %.4f, ", (float)ns->t_datacopy / 1000 / ns->cnt);
    printf("waiting(us): %.4f, ", (float)ns->t_waiting / 1000 / ns->cnt);
    printf("endtoend(us): %.4f, ", (float)ns->t_endtoend / 1000 / ns->cnt);
    printf("\n");
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_shared_nvme_tcp_layer_stat(struct shared_nvme_tcp_layer_stat *ns) {
  printf(HEADER2 "all time stat" RESET "\n");
  int i;
  char *dis_header[9] = {"<4KB", "4KB",   "8KB",    "16KB",  "32KB",
                         "64KB", "128KB", ">128KB", "others"};
  for(i = 0; i < 9; i++){
    printf(HEADER3 "size=%s, \n" RESET, dis_header[i]);
    print_read(&ns->all_time_stat.read[i], ns->all_time_stat.read_before[i]);
    print_write(&ns->all_time_stat.write[i], ns->all_time_stat.write_before[i]);
  }
}

void nvme_tcp_layer_monitor_display(){
  printf(HEADER1 "[NVME TCP LAYER]" RESET "\n");
  print_shared_nvme_tcp_layer_stat(shared_nvme_tcp_stat);
}

void map_ntm_nvmetcp_data() {
  int fd;

  /** map the raw_nvmetcp_stat */
  fd = open("/proc/ntm/nvme_tcp/stat", O_RDONLY);
  if (fd == -1) {
    printf("Failed to open /proc/ntm/nvme_tcp/stat\n");
    exit(EXIT_FAILURE);
  }
  shared_nvme_tcp_stat =
      mmap(NULL, sizeof(struct shared_nvme_tcp_layer_stat), PROT_READ, MAP_SHARED, fd, 0);

  if (shared_nvme_tcp_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/nvme_tcp/stat\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
  close(fd);
}

void unmap_ntm_nvmetcp_data() {
  printf("unmmap_raw_nvmetcp_stat\n");

  if (shared_nvme_tcp_stat) {
    munmap(shared_nvme_tcp_stat, sizeof(struct shared_nvme_tcp_layer_stat));
  }
}

#endif  // U_NVMETCP_LAYER_H