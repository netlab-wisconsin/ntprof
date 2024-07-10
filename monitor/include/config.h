#ifndef _CONFIG_H_
#define _CONFIG_H_

#define MAX_QID 33

typedef enum { _READ, _WRITE, _BOTH } req_type;

typedef struct {
  /** device name */
  char dev[32];
  /** sample rate */
  unsigned int rate;
  /** io type, read or write */
  req_type io_type;
  /** sliding window length */
  int win;
  /** io size, in bytes */
  int io_size;
  /** 
   * allow queue id. 
   * I choose char only because it is small, and do not 
   * need to introduce extra header files. bool is preferred, but 
   * it is in different header file for kernel space and user space.
   * 
   * qid[i]=1 means the i-th queue is set.
   */
  char qid[MAX_QID];
  /** user input string */
  char qstr[MAX_QID];
  /** network packet sample rate */
  unsigned int nrate;

  /** to print detailed tracing info or not */
  short detail;
} Arguments;

#endif  // _CONFIG_H_