//
// Created by yuyuan on 2/9/25.
//

#ifndef REPORT_H
#define REPORT_H

// #include "../host/breakdown.h"
#include "../include/analyze.h"

// static inline void print_read_breakdown(struct read_breakdown *b) {
//     if (b->cnt == 0) {
//         printf("No read records (cnt=0)\n");
//         return;
//     }
//
//     printf("latency breakdown (read):\n");
//
// #define X(field) \
// printf(#field ": %.2f\n", (float)((b->field / b->cnt) / 1000));
//     READ_BREAKDOWN_FIELDS(X)
// #undef X
// }
//
// static inline void print_write_breakdown_s(struct write_breakdown_s *b) {
//     if (b->cnt == 0) {
//         printf("No write records (cnt=0)\n");
//         return;
//     }
//
//     printf("latency breakdown (write):\n");
// #define     X(field) \
//     printf(#field ": %.2f\n", (float)((b->field / b->cnt) / 1000));
//     WRITE_BREAKDOWN_S_FIELDS(X)
// #undef X
// }
//
// static inline void print_write_breakdown_l(struct write_breakdown_l *b) {
//     if (b->cnt == 0) {
//         printf("No write records (cnt=0)\n");
//         return;
//     }
//
//     printf("latency breakdown (write):\n");
// #define     X(field) \
//     printf(#field ": %.2f\n", (float)((b->field / b->cnt) / 1000));
//     WRITE_BREAKDOWN_L_FIELDS(X)
// #undef X
// }

static inline void print_breakdown(struct latency_breakdown* b) {
  if (!b) {
    printf("latency_breakdown: NULL pointer\n");
    return;
  }

  printf("=== Latency Breakdown ===\n");
  printf("Block Layer Submission: %d us\n", b->blk_submission);
  printf("Block Layer Completion: %d us\n", b->blk_completion);
  printf("NVMe TCP Submission: %d us\n", b->nvme_tcp_submission);
  printf("NVMe TCP Completion: %d us\n", b->nvme_tcp_completion);
  printf("NVMet TCP Submission: %d us\n", b->nvmet_tcp_submission);
  printf("NVMet TCP Completion: %d us\n", b->nvmet_tcp_completion);
  printf("Target Subsystem Processing: %d us\n", b->target_subsystem);
  printf("Network Stack Submission (target): %d us\n", b->nstack_submission);
  printf("Network Stack Completion (initiator): %d us\n", b->nstack_completion);
  printf("Network Transmission: %d us\n", b->network_transmission);
  printf("=========================\n");
}

static inline void print_category_summary(struct category_summary* cs) {
  // print the key
  printf("group: type=%s, ", cs->key.io_type ? "write" : "read");
  printf("size=%d, ", cs->key.io_size);
  printf("dev=%s\n", cs->key.session_name);
  print_breakdown(&cs->bd);
}


static inline void print_report(struct report* r) {
  printf("Report:\n");

  int cat_cnt = r->category_num;
  printf("cnt: %d, cat_cnt: %d\n", r->category_num, cat_cnt);
  if (cat_cnt == 0) {
    printf("No records (cnt=0)\n");
    return;
  }
  int i;
  for (i = 0; i < cat_cnt; i++) {
    struct category_summary* b = &r->summary[i];
    print_category_summary(b);
  }
}

#endif //REPORT_H