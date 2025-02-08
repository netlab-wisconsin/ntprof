#ifndef BREAKDOWN_H
#define BREAKDOWN_H

#include "../include/statistics.h"

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
    u64 cnt;
#define X(field) u64 field;
    READ_BREAKDOWN_FIELDS(X)
    #undef X
};

static inline void init_read_breakdown(struct read_breakdown *b) {
    memset(b, 0, sizeof(*b));
}

static inline void print_read_breakdown(struct read_breakdown *b)
{
    if (!b->cnt) {
        pr_info("No read records (cnt=0)\\n");
        return;
    }

    pr_info("latency breakdown (read):\\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field / b->cnt) / 1000));
    READ_BREAKDOWN_FIELDS(X)
#undef X
}

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
    u64 cnt;

#define X(field) u64 field;
    WRITE_BREAKDOWN_S_FIELDS(X)
#undef X
};

static inline void init_write_breakdown_s(struct write_breakdown_s *b)
{
    memset(b, 0, sizeof(*b));
}

static inline void print_write_breakdown_s(struct write_breakdown_s *b)
{
    if (!b->cnt) {
        pr_info("No short-write records (cnt=0)\n");
        return;
    }

    pr_info("latency breakdown (write - short):\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field / b->cnt) / 1000));
    WRITE_BREAKDOWN_S_FIELDS(X)
#undef X
}

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
    u64 cnt;

#define X(field) u64 field;
    WRITE_BREAKDOWN_L_FIELDS(X)
#undef X
};

/* 3. 初始化 */
static inline void init_write_breakdown_l(struct write_breakdown_l *b)
{
    memset(b, 0, sizeof(*b));
}

/* 4. 打印 */
static inline void print_write_breakdown_l(struct write_breakdown_l *b)
{
    if (!b->cnt) {
        pr_info("No long-write records (cnt=0)\n");
        return;
    }

    pr_info("latency breakdown (write - long):\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field / b->cnt) / 1000));
    WRITE_BREAKDOWN_L_FIELDS(X)
#undef X
}

void break_latency_read(struct profile_record *record, struct read_breakdown *breakdown);

void break_latency_write_s(struct profile_record *record, struct write_breakdown_s *breakdown);

void break_latency_write_l(struct profile_record *record, struct write_breakdown_l *breakdown);

#endif //BREAKDOWN_H
