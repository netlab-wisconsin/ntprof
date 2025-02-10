#ifndef ANALYZE_H
#define ANALYZE_H

#include "config.h"



#define READ_BREAKDOWN_FIELDS(X)                 \
X(blk_submission_queueing)                   \
X(nvme_tcp_submission_queueing)              \
X(nvme_tcp_request_processing)               \
X(nvme_tcp_waiting)                          \
X(nvmet_tcp_cmd_capsule_queueing)            \
X(nvmet_tcp_cmd_processing)                  \
X(nvmet_tcp_submit_to_blk)                   \
X(nvmet_tcp_completion_queueing)             \
X(nvmet_tcp_response_processing)             \
X(nvme_tcp_completion_queueing)              \
X(nvme_tcp_response_processing)              \
X(blk_completion_queueing)                   \
X(blk_e2e)                                   \
X(nvme_tcp_e2e)                              \
X(nvmet_tcp_e2e)                             \
X(networking)

struct read_breakdown {
    unsigned long long cnt;
#define X(field) unsigned long long field;
    READ_BREAKDOWN_FIELDS(X)
    #undef X
};


#define WRITE_BREAKDOWN_S_FIELDS(X)               \
X(blk_submission_queueing)                    \
X(nvme_tcp_submission_queueing)               \
X(nvme_tcp_request_processing)                \
X(nvmet_tcp_cmd_capsule_queueing)             \
X(nvmet_tcp_cmd_processing)                   \
X(nvmet_tcp_submit_to_blk)                    \
X(nvmet_tcp_completion_queueing)              \
X(nvmet_tcp_response_processing)              \
X(nvme_tcp_waiting_for_resp)                  \
X(nvme_tcp_completion_queueing)               \
X(blk_completion_queueing)                    \
X(blk_e2e)                                    \
X(nvme_tcp_e2e)                               \
X(nvmet_tcp_e2e)                              \
X(networking)

struct write_breakdown_s {
    unsigned long long cnt;

#define X(field) unsigned long long field;
    WRITE_BREAKDOWN_S_FIELDS(X)
#undef X
};

#define WRITE_BREAKDOWN_L_FIELDS(X)               \
X(blk_submission_queueing)                    \
X(nvme_tcp_cmd_submission_queueing)           \
X(nvme_tcp_cmd_capsule_processing)            \
X(nvmet_tcp_cmd_capsule_queueing)             \
X(nvmet_tcp_cmd_processing)                   \
X(nvmet_tcp_r2t_submission_queueing)          \
X(nvmet_tcp_r2t_resp_processing)              \
X(nvmet_tcp_wait_for_data)                    \
X(nvme_tcp_wait_for_r2t)                      \
X(nvme_tcp_r2t_resp_queueing)                 \
X(nvme_tcp_data_pdu_submission_queueing)      \
X(nvme_tcp_data_pdu_processing)               \
X(nvmet_tcp_write_cmd_queueing)               \
X(nvmet_tcp_write_cmd_processing)             \
X(nvmet_tcp_submit_to_blk)                    \
X(nvmet_tcp_completion_queueing)              \
X(nvmet_tcp_response_processing)              \
X(nvme_tcp_wait_for_resp)                     \
X(nvme_tcp_resp_queueing)                     \
X(blk_completion_queueing)                    \
X(blk_e2e)                                    \
X(networking)


struct write_breakdown_l {
    unsigned long long cnt;

#define X(field) unsigned long long field;
    WRITE_BREAKDOWN_L_FIELDS(X)
#undef X
};

enum EBreakdownType {
    BREAKDOWN_READ,
    BREAKDOWN_WRITES,
    BREAKDOWN_WRITEL
};

// we maintain a hashtable, (category_key --> categorized_records)

struct category_key {
    int io_size;
    int io_type;
    char session_name[MAX_SESSION_NAME_LEN];
};

struct breakdown {
    enum EBreakdownType type;
    union {
        struct read_breakdown read;
        struct write_breakdown_s writes;
        struct write_breakdown_l writel;
    };
};

struct category_summary {
    struct category_key key;
    struct breakdown bd;
};

#define MAX_CATEGORIES 8

struct report {
    unsigned int category_num;
    struct category_summary summary[MAX_CATEGORIES];
};

struct analyze_arg {
    struct ntprof_config config;
    struct report rpt;
};

#endif //ANALYZE_H
