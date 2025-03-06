#ifndef REPORT_H
#define REPORT_H

#include "../include/analyze.h"

static void print_combined(const char* name, long long total_ns, int cnt,
                           const int dist[MAX_DIST_BUCKET]) {
  static const char* percentiles[] = {
      "10%", "50%", "70%", "80%", "90%", "95%", "99%", "99.9%"
  };

  if (cnt == 0) {
    printf("%-25s: N/A\n", name);
    return;
  }

  double avg_us = (double)total_ns / cnt / 1000.0;

  printf("%-25s: avg %.2f us", name, avg_us);

  for (int i = 0; i < MAX_DIST_BUCKET; ++i) {
    double us = dist[i] / 1000.0; // ns -> us
    printf(", %s %.2f", percentiles[i], us);
  }

  printf("\n");
}

static inline void print_latency_breakdown_summary_user(
    const struct latency_breakdown_summary* s) {
  printf("=== Latency Breakdown (avg with distribution) ===\n");

  print_combined("blk_submission", s->bd_sum.blk_submission, s->cnt,
                 s->dist.blk_submission);
  print_combined("blk_completion", s->bd_sum.blk_completion, s->cnt,
                 s->dist.blk_completion);
  print_combined("nvme_tcp_submission", s->bd_sum.nvme_tcp_submission, s->cnt,
                 s->dist.nvme_tcp_submission);
  print_combined("nvme_tcp_completion", s->bd_sum.nvme_tcp_completion, s->cnt,
                 s->dist.nvme_tcp_completion);
  print_combined("nvmet_tcp_submission", s->bd_sum.nvmet_tcp_submission, s->cnt,
                 s->dist.nvmet_tcp_submission);
  print_combined("nvmet_tcp_completion", s->bd_sum.nvmet_tcp_completion, s->cnt,
                 s->dist.nvmet_tcp_completion);
  print_combined("target_subsystem", s->bd_sum.target_subsystem, s->cnt,
                 s->dist.target_subsystem);
  print_combined("nstack_submission", s->bd_sum.nstack_submission, s->cnt,
                 s->dist.nstack_submission);
  print_combined("nstack_completion", s->bd_sum.nstack_completion, s->cnt,
                 s->dist.nstack_completion);
  print_combined("network_transmission", s->bd_sum.network_transmission, s->cnt,
                 s->dist.network_transmission);

  printf("==================================================\n");
}

static inline void print_category_summary(struct category_summary* cs) {
  printf("group: type=%s, ", cs->key.io_type ? "write" : "read");
  printf("size=%d, ", cs->key.io_size);
  printf("dev=%s", cs->key.session_name);
  printf("sample#=: %d\n", cs->lbs.cnt);
  print_latency_breakdown_summary_user(&cs->lbs);
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