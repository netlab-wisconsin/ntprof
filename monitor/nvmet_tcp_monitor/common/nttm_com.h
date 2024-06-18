#ifndef _NTTM_COM_H_
#define _NTTM_COM_H_

enum size_type { _LT_4K, _4K, _8K, _16K, _32K, _64K, _128K, _GT_128K, _OTHERS };

struct nvmet_tcp_read_breakdown {
  unsigned long long in_nvmet_tcp_time;
  unsigned long long in_blk_time;
  unsigned long long end2end_time;
  unsigned long cnt;
};

struct nvmet_tcp_write_breakdown {
  unsigned long long make_r2t_time;
  unsigned long long in_nvmet_tcp_time;
  unsigned long long in_blk_time;
  unsigned long long end2end_time;
  unsigned long cnt;
};

struct nvmet_tcp_stat {
  /** these 2 attributes are summary for the sliding window */
  struct nvmet_tcp_read_breakdown sw_read_breakdown;
  struct nvmet_tcp_write_breakdown sw_write_breakdown;

  /** these 2 attributes are summary for the whole trace */
  struct nvmet_tcp_read_breakdown all_read;
  struct nvmet_tcp_write_breakdown all_write;
};

struct blk_stat {
  unsigned long long sw_read_cnt;
  unsigned long long sw_write_cnt;
  unsigned long long sw_read_io[9];
  unsigned long long sw_write_io[9];
  unsigned long long sw_read_time;
  unsigned long long sw_write_time;
  // unsigned long long in_flight;

  // struct blk_sample_summary all_sample_summary;
  unsigned long long all_read_cnt;
  unsigned long long all_write_cnt;
  unsigned long long all_read_io[9];
  unsigned long long all_write_io[9];
  unsigned long long all_read_time;
  unsigned long long all_write_time;
};

void reset_blk_stat(struct blk_stat *stat) {
  stat->sw_read_cnt = 0;
  stat->sw_write_cnt = 0;
  // stat->in_flight = 0;
  int i;
  for (i = 0; i < 9; i++) {
    stat->sw_read_io[i] = 0;
    stat->sw_write_io[i] = 0;
  }
  stat->sw_read_time = 0;
  stat->sw_write_time = 0;
}

void init_blk_stat(struct blk_stat *stat) {
  reset_blk_stat(stat);
  stat->all_read_cnt = 0;
  stat->all_write_cnt = 0;
  // stat->in_flight = 0;
  int i;
  for (i = 0; i < 9; i++) {
    stat->all_read_io[i] = 0;
    stat->all_write_io[i] = 0;
  }
  stat->all_read_time = 0;
  stat->all_write_time = 0;
}

inline void inc_cnt_arr(unsigned long long *arr, int size) {
  if (size < 4096) {
    arr[_LT_4K]++;
  } else if (size == 4096) {
    arr[_4K]++;
  } else if (size == 8192) {
    arr[_8K]++;
  } else if (size == 16384) {
    arr[_16K]++;
  } else if (size == 32768) {
    arr[_32K]++;
  } else if (size == 65536) {
    arr[_64K]++;
  } else if (size == 131072) {
    arr[_128K]++;
  } else if (size > 131072) {
    arr[_GT_128K]++;
  } else {
    arr[_OTHERS]++;
  }
}

#endif  // _NTTM_COM_H_