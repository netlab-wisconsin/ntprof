#ifndef BREAKDOWN_H
#define BREAKDOWN_H


#include "../include/analyze.h"
#include "../include/statistics.h"

static inline void init_read_breakdown(struct read_breakdown* b) {
  memset(b, 0, sizeof(*b));
}

static inline void print_read_breakdown(struct read_breakdown* b) {
  // if (!b->cnt) {
  //     pr_info("No read records (cnt=0)\\n");
  //     return;
  // }

  pr_info("latency breakdown (read):\\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field ) / 1000));
  READ_BREAKDOWN_FIELDS(X)
#undef X
}


static inline void init_write_breakdown_s(struct write_breakdown_s* b) {
  memset(b, 0, sizeof(*b));
}

static inline void print_write_breakdown_s(struct write_breakdown_s* b) {
  // if (!b->cnt) {
  //     pr_info("No short-write records (cnt=0)\n");
  //     return;
  // }

  pr_info("latency breakdown (write - short):\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field ) / 1000));
  WRITE_BREAKDOWN_S_FIELDS(X)
#undef X
}


static inline void init_write_breakdown_l(struct write_breakdown_l* b) {
  memset(b, 0, sizeof(*b));
}

/* 4. 打印 */
static inline void print_write_breakdown_l(struct write_breakdown_l* b) {
  // if (!b->cnt) {
  //     pr_info("No long-write records (cnt=0)\n");
  //     return;
  // }

  pr_info("latency breakdown (write - long):\n");

#define X(field) \
pr_info(#field ": %lu\n", (unsigned long)((b->field ) / 1000));
  WRITE_BREAKDOWN_L_FIELDS(X)
#undef X
}

static inline void print_breakdown(struct latency_breakdown* b) {
  if (!b) {
    pr_info("latency_breakdown: NULL pointer\n");
    return;
  }

  pr_info("=== Latency Breakdown ===\n");
  pr_info("Block Layer Submission: %d us\n", b->blk_submission);
  pr_info("Block Layer Completion: %d us\n", b->blk_completion);
  pr_info("NVMe TCP Submission: %d us\n", b->nvme_tcp_submission);
  pr_info("NVMe TCP Completion: %d us\n", b->nvme_tcp_completion);
  pr_info("NVMet TCP Submission: %d us\n", b->nvmet_tcp_submission);
  pr_info("NVMet TCP Completion: %d us\n", b->nvmet_tcp_completion);
  pr_info("Target Subsystem Processing: %d us\n", b->target_subsystem);
  pr_info("Network Stack Submission (target): %d us\n", b->nstack_submission);
  pr_info("Network Stack Completion (initiator): %d us\n", b->nstack_completion);
  pr_info("Network Transmission: %d us\n", b->network_transmission);
  pr_info("=========================\n");
}


void break_latency(struct profile_record* record,
                   struct latency_breakdown* breakdown);

// void break_latency_read(struct profile_record* record,
//                         struct read_breakdown* breakdown);

// void break_latency_write_s(struct profile_record* record,
//                            struct write_breakdown_s* breakdown);
//
// void break_latency_write_l(struct profile_record* record,
//                            struct write_breakdown_l* breakdown);

#endif //BREAKDOWN_H