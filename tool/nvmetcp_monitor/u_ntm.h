#ifndef U_NTM_H
#define U_NTM_H


typedef enum {
    READ,
    WRITE,
    BOTH
} req_type;

typedef struct {
    char dev[32];
    float rate;
    req_type io_type;
    int win;
    int size;
    int qid;
    float nrate;
} Arguments;

extern Arguments args;


/** filters */
extern char device_name[32];


#endif  // U_NTM_H