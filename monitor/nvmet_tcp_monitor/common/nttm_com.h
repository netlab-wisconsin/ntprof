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
  struct nvmet_tcp_read_breakdown read_breakdown;
  struct nvmet_tcp_write_breakdown write_breakdown;
};

struct blk_sample_summary{
  unsigned long long total_time;
  unsigned long cnt;
};

inline void init_blk_sample_summary(struct blk_sample_summary *summary) {
  summary->total_time = 0;
  summary->cnt = 0;
}

struct blk_stat{
  /** TOOD separate read and write io */
  struct blk_sample_summary sample_summary;

  unsigned long read_cnt;
  unsigned long write_cnt;
  unsigned long read_io[9];
  unsigned long write_io[9];
  unsigned long long in_flight;
};

void init_blk_stat(struct blk_stat *stat) {
  int i;
  stat->read_cnt = 0;
  stat->write_cnt = 0;
  stat->in_flight = 0;
  for ( i = 0; i < 9; i++) {
    stat->read_io[i] = 0;
    stat->write_io[i] = 0;
  }
  stat->sample_summary.cnt = 0;
  stat->sample_summary.total_time = 0;
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

#endif // _NTTM_COM_H_