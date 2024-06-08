#ifndef NTM_COM_H
#define NTM_COM_H

enum SIZE_TYPE { _LT_4K, _4K, _8K, _16K, _32K, _64K, _128K, _GT_128K, _OTHERS };

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
   * refers to enum SIZE_TYPE
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

#endif // NTM_COM_H