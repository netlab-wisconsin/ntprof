#include "u_nvme_tcp_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ntm_com.h"
#include "output.h"

#define NSEC_PER_SEC 1000000000L
#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static struct shared_nvme_tcp_layer_stat *shared;

static char *stat_path = "/proc/ntm/nvme_tcp/stat";

void print_read(struct nvmetcp_read_breakdown *ns,
                unsigned long long read_before) {
  printf("read breakdown -- ");
  if (ns->cnt) {
    printf("cnt: %d,", ns->cnt);
    printf("blk_proc(us): %.2f, ", (float)read_before / 1000 / ns->cnt);
    printf("sub_q(us): %.2f, ", (float)ns->sub_q / 1000 / ns->cnt);
    printf("req_proc(us): %.2f, ", (float)ns->req_proc / 1000 / ns->cnt);
    printf("waiting(us): %.2f, ", (float)ns->waiting / 1000 / ns->cnt);
    printf("comp_q(us): %.2f, ", (float)ns->comp_q / 1000 / ns->cnt);
    printf("resp_proc(us): %.2f, ", (float)ns->resp_proc / 1000 / ns->cnt);
    printf("e2e(us): %.2f, ", (float)ns->e2e / 1000 / ns->cnt);
    printf("trans(us): %.2f, ", (float)ns->trans / ns->cnt);
    printf("\n");
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_write(struct nvmetcp_write_breakdown *ns,
                 unsigned long long write_before) {
  printf("write breakdown -- ");
  if (ns->cnt) {
    printf("cnt: %d,", ns->cnt);
    printf("blk_proc: %.2f, ", (float)write_before / 1000 / ns->cnt);
    printf("sub_q1(us): %.2f, ", (float)ns->sub_q1 / 1000 / ns->cnt);
    printf("req_proc1(us): %.2f, ", (float)ns->req_proc1 / 1000 / ns->cnt);
    printf("waiting1(us): %.2f, ", (float)ns->waiting1 / 1000 / ns->cnt);
    printf("ready_q(us): %.2f, ", (float)ns->ready_q / 1000 / ns->cnt);
    printf("sub_q2(us): %.2f, ", (float)ns->sub_q2 / 1000 / ns->cnt);
    printf("req_proc2(us): %.2f, ", (float)ns->req_proc2 / 1000 / ns->cnt);
    printf("waiting2(us): %.2f, ", (float)ns->waiting2 / 1000 / ns->cnt);
    printf("comp_q(us): %.2f, ", (float)ns->comp_q / 1000 / ns->cnt);
    printf("e2e(us): %.2f, ", (float)ns->e2e / 1000 / ns->cnt);
    printf("trans1: %.2f, ", (float)ns->trans1 / ns->cnt);
    printf("trans2: %.2f, ", (float)ns->trans2 / ns->cnt2);
    printf("\n");
  } else {
    printf("cnt: %d\n", ns->cnt);
  }
}

void print_throughput_info(struct shared_nvme_tcp_layer_stat *s) {
  printf("throughput: \n");
  int i;
  for (int i = 0; i < MAX_QID; i++) {
    long long start = s->tp[i].first_ts;
    long long end = s->tp[i].last_ts;
    int duration = (end - start) / NSEC_PER_SEC;
    printf("qid=%d, duration:%d, ", i, duration);
    int j;
    for (j = 0; j < SIZE_NUM; j++) {
      // printf("r[%s]:%.2f, ", size_name(j),
      // ((double)(s->tp[i].read_cnt[j]))/duration);
      if (s->tp[i].read_cnt[j] != 0)
        printf("r[%s]:%.2f, ", size_name(j),
               ((double)(s->tp[i].read_cnt[j])) / duration);
    }
    for (j = 0; j < SIZE_NUM; j++) {
      // printf("w[%s]:%.2f, ", size_name(j),
      // ((double)(s->tp[i].write_cnt[j]))/duration);
      if (s->tp[i].write_cnt[j] != 0)
        printf("w[%s]:%.2f, ", size_name(j),
               ((double)(s->tp[i].write_cnt[j])) / duration);
    }
    printf("\n");
  }
}

void print_shared_nvme_tcp_layer_stat(struct shared_nvme_tcp_layer_stat *ns) {
  printf(HEADER2 "all time stat" RESET "\n");
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    printf(HEADER3 "size=%s, \n" RESET, size_name(i));
    print_read(&ns->all_time_stat.read[i], ns->all_time_stat.read_before[i]);
    print_write(&ns->all_time_stat.write[i], ns->all_time_stat.write_before[i]);
  }
  // print_batch_info(&ns->all_time_stat);
}

void nvme_tcp_layer_monitor_display() {
  printf(HEADER1 "[NVME TCP LAYER]" RESET "\n");
  print_shared_nvme_tcp_layer_stat(shared);
  // print_throughput_info(shared);
}

void map_ntm_nvmetcp_data() {
  int fd;

  /** map the raw_nvmetcp_stat */
  fd = open(stat_path, O_RDONLY);
  if (fd == -1) {
    printf("Failed to open %s\n", stat_path);
    exit(EXIT_FAILURE);
  }

  size_t size = PAGE_ALIGN(sizeof(struct shared_nvme_tcp_layer_stat));
  shared = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

  if (shared == MAP_FAILED) {
    printf("Failed to mmap %s\n", stat_path);
    close(fd);
    exit(EXIT_FAILURE);
  }
  close(fd);
}

void unmap_ntm_nvmetcp_data() {
  size_t size =
      (sizeof(struct shared_nvme_tcp_layer_stat) + getpagesize() - 1) &
      ~(getpagesize() - 1);
  if (shared) munmap(shared, size);
}

void init_nvme_tcp_layer_monitor() { map_ntm_nvmetcp_data(); }

void exit_nvme_tcp_layer_monitor() { unmap_ntm_nvmetcp_data(); }
