#ifndef NTM_COM_H
#define NTM_COM_H



typedef enum {
    _READ,
    _WRITE,
    _BOTH
} req_type;



typedef struct {
  /** device name */
    char dev[32];
    /** sample rate */
    float rate;
    /** io type, read or write */
    req_type io_type;
    /** sliding window length */
    int win;
    /** io size, in bytes */
    int io_size;
    /** queue id */
    int qid;
    /** network packet sample rate */
    float nrate;
} Arguments;



enum size_type { _LT_4K, _4K, _8K, _16K, _32K, _64K, _128K, _GT_128K, _OTHERS };


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
  }
  tr->pending_rq = 0;
}

/**
 * Increase the count of a specific size category
 * @param arr the array to store the count of different size categories
 * @param size the size of the io
*/
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

struct blk_lat_stat{
  unsigned long long sum_blk_layer_lat;
  unsigned long long cnt;
};

inline void init_blk_lat_stat(struct blk_lat_stat *stat) {
  stat->sum_blk_layer_lat = 0;
  stat->cnt = 0;
}

struct normal_struct {
  unsigned long item1;
};

inline void init_normal_struct(struct normal_struct *ns) {
  ns->item1 = 0;
}


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
  struct nvmetcp_read_breakdown read;
  struct nvmetcp_write_breakdown write;
  struct blk_lat_stat blk_lat;
};



#endif // NTM_COM_H