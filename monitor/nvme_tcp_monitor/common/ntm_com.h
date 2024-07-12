#ifndef NTM_COM_H
#define NTM_COM_H

#include "config.h"

#define SIZE_NUM 7

enum size_type { _4K, _8K, _16K, _32K, _64K, _128K, _OTHERS };

/** a function, given int size, return enum */ 
static inline enum size_type size_to_enum(int size) {
  if (size == 4096) return _4K;
  if (size == 8192) return _8K;
  if (size == 16384) return _16K;
  if (size == 32768) return _32K;
  if (size == 65536) return _64K;
  if (size == 131072) return _128K;
  return _OTHERS;
}

static inline int size_idx(int size) {
  if (size == 4096) return 0;
  if (size == 8192) return 1;
  if (size == 16384) return 2;
  if (size == 32768) return 3;
  if (size == 65536) return 4;
  if (size == 131072) return 5;
  return 6;
}

static inline char* size_name(int idx) {
  switch (idx) {
    case 0: return "4K";
    case 1: return "8K";
    case 2: return "16K";
    case 3: return "32K";
    case 4: return "64K";
    case 5: return "128K";
    case 6: return "OTHERS";
    default: return "UNKNOWN";
  }
}

/** -------------- BLOCK LAYER -------------- */

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
  unsigned long long read_io[SIZE_NUM];
  /** write io number of different sizes */
  unsigned long long write_io[SIZE_NUM];
  /** TODO: number of io in-flight */
  // unsigned long long pending_rq;

  unsigned long long read_lat;
  unsigned long long write_lat;
  unsigned long long read_io_lat[SIZE_NUM];
  unsigned long long write_io_lat[SIZE_NUM];
};


/**
 * Initialize the blk_stat structure
 * set all the fields to 0
 * @param stat the blk_stat structure to be initialized
 */
static inline void init_blk_stat(struct blk_stat *stat) {
  int i;
  stat->read_count = 0;
  stat->write_count = 0;
  for (i = 0; i < SIZE_NUM; i++) {
    stat->read_io[i] = 0;
    stat->write_io[i] = 0;
    stat->read_io_lat[i] = 0;
    stat->write_io_lat[i] = 0;
  }
  // stat->pending_rq = 0;
  stat->read_lat = 0;
  stat->write_lat = 0;
}

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
*/
static inline void inc_cnt_arr(unsigned long long *arr, int size) {
  arr[size_to_enum(size)]++;
}

static inline void add_lat_arr(unsigned long long *arr, int size, unsigned long long lat) {
  arr[size_to_enum(size)] += lat;
}

/** this struct is shared between kernel space and the user space */
struct shared_blk_layer_stat{
  struct blk_stat all_time_stat;
};

static inline void init_shared_blk_layer_stat(struct shared_blk_layer_stat *stat) {
  init_blk_stat(&stat->all_time_stat);
}


/** -------------- NVME-TCP LAYER -------------- */

struct nvmetcp_read_breakdown {
  int cnt;
  long long sub_q;
  long long req_proc;
  long long waiting;
  long long comp_q;
  long long resp_proc;
  long long e2e;
};

static inline void init_nvmetcp_read_breakdown(struct nvmetcp_read_breakdown *rb) {
  rb->cnt = 0;
  rb->sub_q = 0;
  rb->req_proc = 0;
  rb->waiting = 0;
  rb->comp_q = 0;
  rb->resp_proc = 0;
  rb->e2e = 0;
}

struct nvmetcp_write_breakdown {
  int cnt;
  long long sub_q1;
  long long req_proc1;
  long long waiting1;
  long long ready_q;
  long long sub_q2;
  long long req_proc2;
  long long waiting2;
  long long comp_q;
  long long e2e;
};

static inline void init_nvmetcp_write_breakdown(struct nvmetcp_write_breakdown *wb) {
  wb->cnt = 0;
  wb->sub_q1 = 0;
  wb->req_proc1 = 0;
  wb->waiting1 = 0;
  wb->ready_q = 0;
  wb->sub_q2 = 0;
  wb->req_proc2 = 0;
  wb->waiting2 = 0;
  wb->comp_q = 0;
  wb->e2e = 0;
}

struct nvme_tcp_stat {
  struct nvmetcp_read_breakdown read[SIZE_NUM];
  struct nvmetcp_write_breakdown write[SIZE_NUM];
  unsigned long long read_before[SIZE_NUM];
  unsigned long long write_before[SIZE_NUM];
};

static inline void init_nvme_tcp_stat(struct nvme_tcp_stat *stat) {
  int i;
  for (i = 0; i < SIZE_NUM; i++) {
    init_nvmetcp_read_breakdown(&stat->read[i]);
    init_nvmetcp_write_breakdown(&stat->write[i]);
    stat->read_before[i] = 0;
    stat->write_before[i] = 0;
  }
}

struct shared_nvme_tcp_layer_stat {
  struct nvme_tcp_stat all_time_stat;
};

static inline void init_shared_nvme_tcp_layer_stat(struct shared_nvme_tcp_layer_stat *stat) {
  init_nvme_tcp_stat(&stat->all_time_stat);
}


/** -------------- TCP LAYER -------------- */

struct tcp_stat_one_queue {
  int pkt_in_flight;
  int cwnd;
  char last_event[64];
};

struct tcp_stat {
  struct tcp_stat_one_queue sks[MAX_QID];
};

static inline void init_tcp_stat(struct tcp_stat *stat) {
  int i;
  for (i = 0; i < MAX_QID; i++) {
    stat->sks[i].pkt_in_flight = 0;
    stat->sks[i].cwnd = 0;
    stat->sks[0].last_event[0] = '\0';
  }
}

#endif // NTM_COM_H