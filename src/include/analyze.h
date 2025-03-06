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

// struct breakdown {
//   enum EBreakdownType type;
//
//   struct
//
//   union {
//     struct read_breakdown read;
//     struct write_breakdown_s writes;
//     struct write_breakdown_l writel;
//   };
// };

struct latency_breakdown {
  long long blk_submission;
  long long blk_completion;

  long long nvme_tcp_submission;
  long long nvme_tcp_completion;

  long long nvmet_tcp_submission;
  long long nvmet_tcp_completion;

  long long target_subsystem;

  long long nstack_submission; // target
  long long nstack_completion; // initiator
  long long network_transmission;
};

#define MAX_DIST_BUCKET 8
// 10, 50, 70, 80, 90, 95, 99, 99.9

struct latency_distribution {
  int blk_submission[MAX_DIST_BUCKET];
  int blk_completion[MAX_DIST_BUCKET];

  int nvme_tcp_submission[MAX_DIST_BUCKET];
  int nvme_tcp_completion[MAX_DIST_BUCKET];

  int nvmet_tcp_submission[MAX_DIST_BUCKET];
  int nvmet_tcp_completion[MAX_DIST_BUCKET];

  int target_subsystem[MAX_DIST_BUCKET];

  int nstack_submission[MAX_DIST_BUCKET]; // target
  int nstack_completion[MAX_DIST_BUCKET]; // initiator
  int network_transmission[MAX_DIST_BUCKET];
};

struct latency_breakdown_summary {
  int cnt;
  struct latency_breakdown bd_sum;
  struct latency_distribution dist;
};


struct category_summary {
  struct category_key key;
  struct latency_breakdown_summary lbs;
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