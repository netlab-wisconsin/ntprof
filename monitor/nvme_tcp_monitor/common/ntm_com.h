#ifndef NTM_COM_H
#define NTM_COM_H

#include "config.h"


enum size_type { _LT_4K, _4K, _8K, _16K, _32K, _64K, _128K, _GT_128K, _OTHERS };

/** a function, given int size, return enum */ 
inline enum size_type size_to_enum(int size) {
  if (size < 4096) {
    return _LT_4K;
  } else if (size == 4096) {
    return _4K;
  } else if (size == 8192) {
    return _8K;
  } else if (size == 16384) {
    return _16K;
  } else if (size == 32768) {
    return _32K;
  } else if (size == 65536) {
    return _64K;
  } else if (size == 131072) {
    return _128K;
  } else if (size > 131072) {
    return _GT_128K;
  } else {
    return _OTHERS;
  }
}

/**
 * a data structure to store the blk layer statistics
 * This structure is for communication between kernel and user space.
 * This data structure is NOT thread safe.
 */
struct blk_stat {
  /** total number of read io */
  unsigned long long read_count;
  /** total number of write io */
  unsigned long long write_count;
  /**
   * read io number of different sizes
   * the sizs are divided into 9 categories
   * refers to enum size_type
   */
  unsigned long long read_io[9];
  /** write io number of different sizes */
  unsigned long long write_io[9];
  /** TODO: number of io in-flight */
  unsigned long long pending_rq;

  unsigned long long read_lat;
  unsigned long long write_lat;
  unsigned long long read_io_lat[9];
  unsigned long long write_io_lat[9];
};


/**
 * Initialize the blk_stat structure
 * set all the fields to 0
 * @param tr the blk_stat structure to be initialized
 */
inline void init_blk_tr(struct blk_stat *tr) {
  int i;
  tr->read_count = 0;
  tr->write_count = 0;
  for (i = 0; i < 9; i++) {
    tr->read_io[i] = 0;
    tr->write_io[i] = 0;
    tr->read_io_lat[i] = 0;
    tr->write_io_lat[i] = 0;
  }
  tr->pending_rq = 0;
  tr->read_lat = 0;
  tr->write_lat = 0;
}

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
*/
inline void inc_cnt_arr(unsigned long long *arr, int size) {
  arr[size_to_enum(size)]++;
}

/** this struct is shared between kernel space and the user space */
struct shared_blk_layer_stat{
  struct blk_stat all_time_stat;
};


struct nvmetcp_read_breakdown {
  int cnt;
  unsigned long long t_inqueue;
  unsigned long long t_reqcopy;
  unsigned long long t_datacopy;
  unsigned long long t_waiting;
  unsigned long long t_waitproc;
  unsigned long long t_endtoend;
};



struct nvmetcp_write_breakdown {
  int cnt;
  unsigned long long t_inqueue;
  unsigned long long t_reqcopy;
  unsigned long long t_datacopy;
  unsigned long long t_waiting;
  unsigned long long t_endtoend;
};

struct nvme_tcp_stat {
  struct nvmetcp_read_breakdown read[9];
  struct nvmetcp_write_breakdown write[9];
  unsigned long long read_before[9];
  unsigned long long write_before[9];
};

struct shared_nvme_tcp_layer_stat {
  struct nvme_tcp_stat all_time_stat;
};

struct tcp_stat_one_queue {
  int pkt_in_flight;
  int cwnd;
  char last_event[64];
};

struct tcp_stat {
  struct tcp_stat_one_queue sks[MAX_QID];
};

inline void init_tcp_stat(struct tcp_stat *stat) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    stat->sks[i].pkt_in_flight = 0;
    stat->sks[i].cwnd = 0;
  }
  stat->sks[0].last_event[0] = '\0';
}

#endif // NTM_COM_H