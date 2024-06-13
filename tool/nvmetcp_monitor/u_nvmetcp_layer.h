#ifndef U_NVMETCP_LAYER_H
#define U_NVMETCP_LAYER_H

#include "ntm_com.h"



struct nvmetcp_stat_set {
  struct nvmetcp_stat *raw_nvmetcp_stat;
};

static struct nvmetcp_stat_set *nvmetcp_set;

void init_ntm_nvmetcp(struct nvmetcp_stat_set *_set) {
  nvmetcp_set = _set;
}

void print_nvmetcp_stat(struct nvmetcp_stat *ns, char *header) {
  printf("header: %s \t", header);
  printf("sum_blk_layer_lat(us): %llu\t", ns->sum_blk_layer_lat / 1000);
  printf("cnt: %llu\t", ns->cnt);
  if(ns->cnt > 0)
    printf("average(us): %llu\n", ns->sum_blk_layer_lat / ns->cnt / 1000);
}

void print_nvmetcp_stat_set (struct nvmetcp_stat_set *ns, bool clear) {
  if (clear) printf("\033[H\033[J");
  if(!ns){
    printf("nvmetcp_set is not initialized\n");
  } else if (!ns->raw_nvmetcp_stat) {
    printf("nvmetcp_set->raw_nvmetcp_stat is not initialized\n");
  } else {
    print_nvmetcp_stat(ns->raw_nvmetcp_stat, "RAW NVMETCP STAT");
  }
  // print_nvmetcp_stat(ns->raw_nvmetcp_stat, "RAW NVMETCP STAT");
}

void map_ntm_nvmetcp_data() {
  if (!nvmetcp_set) {
    printf("nvmetcp_set is not initialized\n");
    return;
  }

  int raw_nvmetcp_stat_fd;

  /** map the raw_nvmetcp_stat */
  raw_nvmetcp_stat_fd = open("/proc/ntm/nvmetcp/ntm_raw_nvmetcp_stat", O_RDONLY);
  if (raw_nvmetcp_stat_fd == -1) {
    printf("Failed to open /proc/ntm/nvmetcp/ntm_raw_nvmetcp_stat\n");
    exit(EXIT_FAILURE);
  }
  nvmetcp_set->raw_nvmetcp_stat =mmap(NULL, sizeof(struct nvmetcp_stat), PROT_READ,
                               MAP_SHARED, raw_nvmetcp_stat_fd, 0);
  if (nvmetcp_set->raw_nvmetcp_stat == MAP_FAILED) {
    printf("Failed to mmap /proc/ntm/nvmetcp/ntm_raw_nvmetcp_stat\n");
    close(raw_nvmetcp_stat_fd);
    exit(EXIT_FAILURE);
  }
  printf("nvmetcp_set result, cnt: %llu, sum_blk_layer_lat: %llu\n", nvmetcp_set->raw_nvmetcp_stat->cnt, nvmetcp_set->raw_nvmetcp_stat->sum_blk_layer_lat);
  close(raw_nvmetcp_stat_fd);
}

void unmap_ntm_nvmetcp_data() {
  printf("unmmap_raw_nvmetcp_stat\n");
  if (!nvmetcp_set) {
    printf("nvmetcp_set is not initialized\n");
    return;
  }

  munmap(nvmetcp_set->raw_nvmetcp_stat, sizeof(struct nvmetcp_stat));
}

#endif  // U_NVMETCP_LAYER_H