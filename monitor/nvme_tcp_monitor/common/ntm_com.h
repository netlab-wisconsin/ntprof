#ifndef NTM_COM_H
#define NTM_COM_H

#include "config.h"

#define READ_SIZE_NUM 7
enum read_size_type { _R4K, _R8K, _R16K, _R32K, _R64K, _R128K, _ROTHERS };
#define WRITE_SIZE_NUM 8
enum write_size_type {
  _W4K,
  _W8K,
  _W16K,
  _W32K,
  _W64K,
  _W128K,
  _SWOTHERS,
  _BWOTHERS
};

static inline enum read_size_type read_size_to_enum(int size) {
  if (size == 4096) return _R4K;
  if (size == 8192) return _R8K;
  if (size == 16384) return _R16K;
  if (size == 32768) return _R32K;
  if (size == 65536) return _R64K;
  if (size == 131072) return _R128K;
  return _ROTHERS;
}

static inline enum write_size_type write_size_to_enum(int size,
                                                      short contains_r2t) {
  if (size == 4096) return _W4K;
  if (size == 8192) return _W8K;
  if (size == 16384) return _W16K;
  if (size == 32768) return _W32K;
  if (size == 65536) return _W64K;
  if (size == 131072) return _W128K;
  if (contains_r2t) return _BWOTHERS;
  return _SWOTHERS;
}

static inline char *read_size_name(int idx) {
  switch (idx) {
    case _R4K:
      return "4K";
    case _R8K:
      return "8K";
    case _R16K:
      return "16K";
    case _R32K:
      return "32K";
    case _R64K:
      return "64K";
    case _R128K:
      return "128K";
    case _ROTHERS:
      return "OTHERS";
    default:
      return "UNKNOWN";
  }
}

static inline char *write_size_name(int idx) {
  switch (idx) {
    case _W4K:
      return "4K";
    case _W8K:
      return "8K";
    case _W16K:
      return "16K";
    case _W32K:
      return "32K";
    case _W64K:
      return "64K";
    case _W128K:
      return "128K";
    case _SWOTHERS:
      return "SWOTHERS";
    case _BWOTHERS:
      return "BWOTHERS";
    default:
      return "UNKNOWN";
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
  unsigned long long read_io[READ_SIZE_NUM];
  /** write io number of different sizes */
  unsigned long long write_io[WRITE_SIZE_NUM];
  /** TODO: number of io in-flight */
  // unsigned long long pending_rq;

  unsigned long long read_lat;
  unsigned long long write_lat;
  unsigned long long read_io_lat[READ_SIZE_NUM];
  unsigned long long write_io_lat[WRITE_SIZE_NUM];
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
  for (i = 0; i < READ_SIZE_NUM; i++) {
    stat->read_io[i] = 0;
    stat->read_io_lat[i] = 0;
  }
  for (i = 0; i < WRITE_SIZE_NUM; i++) {
    stat->write_io[i] = 0;
    stat->write_io_lat[i] = 0;
  }
  // stat->pending_rq = 0;
  stat->read_lat = 0;
  stat->write_lat = 0;
}

// /**
//  * Increase the count of a specific size category
//  * @param arr the array to store the count of different size categories
//  * @param size the size of the io
//  */
// static inline void inc_cnt_arr(unsigned long long *arr, int size) {
//   arr[size_to_enum(size)]++;
// }

// static inline void add_lat_arr(unsigned long long *arr, int size,
//                                unsigned long long lat) {
//   arr[size_to_enum(size)] += lat;
// }

/** this struct is shared between kernel space and the user space */
struct shared_blk_layer_stat {
  struct blk_stat all_time_stat;
};

static inline void init_shared_blk_layer_stat(
    struct shared_blk_layer_stat *stat) {
  init_blk_stat(&stat->all_time_stat);
}

/** -------------- NVME-TCP LAYER -------------- */

struct nvmetcp_flush_breakdown {
  int cnt;
  long long sub_q;
  long long req_proc;
  long long waiting;
  long long comp_q;
  long long e2e;
};

static inline void init_nvmetcp_flush_breakdown(
    struct nvmetcp_flush_breakdown *fb) {
  fb->cnt = 0;
  fb->sub_q = 0;
  fb->req_proc = 0;
  fb->waiting = 0;
  fb->comp_q = 0;
  fb->e2e = 0;
}

struct nvmetcp_read_breakdown {
  int cnt;
  long long sub_q;
  long long req_proc;
  long long waiting;
  long long comp_q;
  long long resp_proc;
  long long e2e;
  long long trans;
};

static inline void init_nvmetcp_read_breakdown(
    struct nvmetcp_read_breakdown *rb) {
  rb->cnt = 0;
  rb->sub_q = 0;
  rb->req_proc = 0;
  rb->waiting = 0;
  rb->comp_q = 0;
  rb->resp_proc = 0;
  rb->e2e = 0;
  rb->trans = 0;
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

  long long trans1;
  long long trans2;
  int cnt2;
};

static inline void init_nvmetcp_write_breakdown(
    struct nvmetcp_write_breakdown *wb) {
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
  wb->trans1 = 0;
  wb->trans2 = 0;
  wb->cnt2 = 0;
}

#define MAX_BATCH_SIZE 64
struct nvme_tcp_stat {
  struct nvmetcp_read_breakdown read[READ_SIZE_NUM];
  struct nvmetcp_write_breakdown write[WRITE_SIZE_NUM];
  struct nvmetcp_flush_breakdown flush;
  unsigned long long read_before[READ_SIZE_NUM];
  unsigned long long write_before[WRITE_SIZE_NUM];
  unsigned long long flush_before;
  /** histogram for request type */
  /* 0. flush
     1. read, 0K < s <= 4K
     2. read, 4K < s <= 8K
     3. read, 8K < s <= 16K
     4. read, 16K < s <= 32K
     5. read, 32K < s <= 64K
     6. read, 64K < s <= 128K
     7. read, s > 128K
     8. write, 0K < s <= 4K
     9. write, 4K < s <= 8K
     10. write, 8K < s <= 16K
     11. write, 16K < s <= 32K
     12. write, 32K < s <= 64K
     13. write, 64K < s <= 128K
     14. write, s > 128K
     15. others
  */
  long long req_type_hist[16];
};

static inline void init_nvme_tcp_stat(struct nvme_tcp_stat *stat) {
  int i;
  for(i = 0; i < READ_SIZE_NUM; i++) {
    init_nvmetcp_read_breakdown(&stat->read[i]);
    stat->read_before[i] = 0;
  }
  for(i = 0; i < WRITE_SIZE_NUM; i++) {
    init_nvmetcp_write_breakdown(&stat->write[i]);
    stat->write_before[i] = 0;
  }
  init_nvmetcp_flush_breakdown(&stat->flush);
  stat->flush_before = 0;
}

struct nvme_tcp_throughput {
  long long first_ts;
  long long last_ts;
  long read_cnt[READ_SIZE_NUM];
  long write_cnt[WRITE_SIZE_NUM];
};

static inline void init_nvme_tcp_throughput(struct nvme_tcp_throughput *tp) {
  tp->first_ts = 0;
  tp->last_ts = 0;
  int i;
  for(i = 0; i < READ_SIZE_NUM; i++) {
    tp->read_cnt[i] = 0;
  }
  for(i = 0; i < WRITE_SIZE_NUM; i++) {
    tp->write_cnt[i] = 0;
  }
}

struct shared_nvme_tcp_layer_stat {
  struct nvme_tcp_stat all_time_stat;
  struct nvme_tcp_throughput tp[MAX_QID];
};

static inline void init_shared_nvme_tcp_layer_stat(
    struct shared_nvme_tcp_layer_stat *stat) {
  init_nvme_tcp_stat(&stat->all_time_stat);
}

/** -------------- TCP LAYER -------------- */

struct tcp_stat_one_queue {
  int pkt_in_flight;
  int cwnd;
  char last_event[64];
  long long skb_num;
  long long skb_size;
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

#endif  // NTM_COM_H