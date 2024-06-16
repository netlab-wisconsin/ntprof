#ifndef _CONFIG_H_
#define _CONFIG_H_


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

#endif // _CONFIG_H_