#ifndef _NTTM_COM_H_
#define _NTTM_COM_H_

#include "config.h"

#define READ_SIZE_NUM 7
#define WRITE_SIZE_NUM 8

enum read_size_type { _R4K, _R8K, _R16K, _R32K, _R64K, _R128K, _ROTHERS };
enum write_size_type {
  _W4K,
  _W8K,
  _W16K,
  _W32K,
  _W64K,
  _W128K,
  _WSOTHERS,
  _WBOTHERS
};

/** a function, given int size, return enum */
static inline enum read_size_type read_size_to_enum(int size) {
  if (size == 4096) return _R4K;
  if (size == 8192) return _R8K;
  if (size == 16384) return _R16K;
  if (size == 32768) return _R32K;
  if (size == 65536) return _R64K;
  if (size == 131072) return _R128K;
  return _ROTHERS;
}

static inline enum write_size_type write_size_to_enum(int size, short contains_r2t) {
  if (size == 4096) return _W4K;
  if (size == 8192) return _W8K;
  if (size == 16384) return _W16K;
  if (size == 32768) return _W32K;
  if (size == 65536) return _W64K;
  if (size == 131072) return _W128K;
  if (contains_r2t) return _WBOTHERS;
  return _WSOTHERS;
}

// static inline int size_idx(int size) {
//   if (size == 4096) return 0;
//   if (size == 8192) return 1;
//   if (size == 16384) return 2;
//   if (size == 32768) return 3;
//   if (size == 65536) return 4;
//   if (size == 131072) return 5;
//   return 6;
// }

// static inline char *size_name(int idx) {
//   switch (idx) {
//     case 0:
//       return "4K";
//     case 1:
//       return "8K";
//     case 2:
//       return "16K";
//     case 3:
//       return "32K";
//     case 4:
//       return "64K";
//     case 5:
//       return "128K";
//     case 6:
//       return "OTHERS";
//     default:
//       return "UNKNOWN";
//   }
// }

static inline char *read_size_name(enum read_size_type idx) {
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

static inline char *write_size_name(enum write_size_type idx) {
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
    case _WSOTHERS:
      return "OTHERS_WO_R2T";
    case _WBOTHERS:
      return "OTHERS_WI_R2T";
    default:
      return "UNKNOWN";
  }
}

/** -------------- BLK LAYER -------------- */

struct blk_stat {
  // struct blk_sample_summary all_sample_summary;
  unsigned long long all_read_cnt;
  unsigned long long all_write_cnt;
  unsigned long long all_read_io[READ_SIZE_NUM];
  unsigned long long all_write_io[WRITE_SIZE_NUM];
  unsigned long long all_read_time[READ_SIZE_NUM];
  unsigned long long all_write_time[WRITE_SIZE_NUM];
};

static inline void init_blk_stat(struct blk_stat *stat) {
  int i;
  stat->all_read_cnt = 0;
  stat->all_write_cnt = 0;
  // stat->in_flight = 0;
  for (i = 0; i < READ_SIZE_NUM; i++) {
    stat->all_read_io[i] = 0;
    stat->all_read_time[i] = 0;
  }
  for (i = 0; i < WRITE_SIZE_NUM; i++) {
    stat->all_write_io[i] = 0;
    stat->all_write_time[i] = 0;
  }
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

static inline void inc_read_cnt_arr(unsigned long long *arr, int size) {
  arr[read_size_to_enum(size)]++;
}

static inline void add_read_lat_arr(unsigned long long *arr, int size,
                                    unsigned long long lat) {
  arr[read_size_to_enum(size)] += lat;
}

static inline void inc_write_cnt_arr(unsigned long long *arr, int size,
                                     short contains_r2t) {
  arr[write_size_to_enum(size, contains_r2t)]++;
}

static inline void add_write_lat_arr(unsigned long long *arr, int size,
                                     unsigned long long lat, short contains_r2t) {
  arr[write_size_to_enum(size, contains_r2t)] += lat;
}

/** -------------- NVME-TCP LAYER -------------- */

struct nvmet_tcp_flush_breakdown {
  unsigned long long cmd_caps_q;
  unsigned long long cmd_proc;
  unsigned long long sub_and_exec;
  unsigned long long comp_q;
  unsigned long long resp_proc;
  unsigned long long e2e;
  unsigned long cnt;
};

static inline void init_nvmet_tcp_flush_breakdown(
    struct nvmet_tcp_flush_breakdown *breakdown) {
  breakdown->cmd_caps_q = 0;
  breakdown->cmd_proc = 0;
  breakdown->sub_and_exec = 0;
  breakdown->comp_q = 0;
  breakdown->resp_proc = 0;
  breakdown->e2e = 0;
  breakdown->cnt = 0;
}

struct nvmet_tcp_read_breakdown {
  unsigned long long cmd_caps_q;
  unsigned long long cmd_proc;
  unsigned long long sub_and_exec;
  unsigned long long comp_q;
  unsigned long long resp_proc;
  unsigned long long end2end;
  unsigned long cnt;

  unsigned long long trans;
};

static inline void init_nvmet_tcp_read_breakdown(
    struct nvmet_tcp_read_breakdown *breakdown) {
  breakdown->cmd_caps_q = 0;
  breakdown->cmd_proc = 0;
  breakdown->sub_and_exec = 0;
  breakdown->comp_q = 0;
  breakdown->resp_proc = 0;
  breakdown->end2end = 0;
  breakdown->cnt = 0;
  breakdown->trans = 0;
}

struct nvmet_tcp_write_breakdown {
  long long cmd_caps_q;
  long long cmd_proc;
  long long r2t_sub_q;
  long long r2t_resp_proc;
  long long wait_for_data;
  long long write_cmd_q;
  long long write_cmd_proc;
  long long nvme_sub_exec;
  long long comp_q;
  long long resp_proc;
  long long e2e;
  long cnt;
  long long trans1;
  long long trans2;
  long cnt2;
};

static inline void init_nvmet_tcp_write_breakdown(
    struct nvmet_tcp_write_breakdown *breakdown) {
  breakdown->cmd_caps_q = 0;
  breakdown->cmd_proc = 0;
  breakdown->r2t_sub_q = 0;
  breakdown->r2t_resp_proc = 0;
  breakdown->wait_for_data = 0;
  breakdown->write_cmd_q = 0;
  breakdown->write_cmd_proc = 0;
  breakdown->nvme_sub_exec = 0;
  breakdown->comp_q = 0;
  breakdown->resp_proc = 0;
  breakdown->e2e = 0;
  breakdown->cnt = 0;
  breakdown->trans1 = 0;
  breakdown->trans2 = 0;
  breakdown->cnt2 = 0;
}

struct nvmet_tcp_throughput_one_queue {
  long long first_ts;
  long long last_ts;
  long long read_cnt[MAX_QID];
  long long write_cnt[MAX_QID];
};

#define MAX_BATCH_SIZE 64
struct nvmet_tcp_stat {
  /** these 2 attributes are summary for the whole trace */
  struct nvmet_tcp_read_breakdown all_read[READ_SIZE_NUM];
  struct nvmet_tcp_write_breakdown all_write[WRITE_SIZE_NUM];
  struct nvmet_tcp_flush_breakdown flush;
  long long recv_cnt;
  long long recv;
  long long send_cnt;
  long long send;

  struct nvmet_tcp_throughput_one_queue throughput[MAX_QID];
};

static inline void init_nvmet_tcp_throughput_one_queue(
    struct nvmet_tcp_throughput_one_queue *throughput) {
  throughput->first_ts = 0;
  throughput->last_ts = 0;
  int i;
  for (i = 0; i < MAX_QID; i++) {
    throughput->read_cnt[i] = 0;
    throughput->write_cnt[i] = 0;
  }
}

static inline void init_nvmet_tcp_stat(struct nvmet_tcp_stat *stat) {
  int i;
  for (i = 0; i < READ_SIZE_NUM; i++) {
    init_nvmet_tcp_read_breakdown(&stat->all_read[i]);
  }
  for (i = 0; i < WRITE_SIZE_NUM; i++) {
    init_nvmet_tcp_write_breakdown(&stat->all_write[i]);
  }

  stat->recv_cnt = 0;
  stat->recv = 0;
  stat->send_cnt = 0;
  stat->send = 0;

  for (i = 0; i < MAX_QID; i++) {
    init_nvmet_tcp_throughput_one_queue(&stat->throughput[i]);
  }
  init_nvmet_tcp_flush_breakdown(&stat->flush);
}

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
    stat->sks[i].last_event[0] = '\0';
  }
}

#endif  // _NTTM_COM_H_