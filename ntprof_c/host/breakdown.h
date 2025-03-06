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
  pr_info("Network Stack Completion (initiator): %d us\n",
          b->nstack_completion);
  pr_info("Network Transmission: %d us\n", b->network_transmission);
  pr_info("=========================\n");
}

static void print_combined(const char* name, long long total_ns, int cnt,
                           const int dist[MAX_DIST_BUCKET]) {
  static const char* percentiles[] = {
      "10%", "50%", "70%", "80%", "90%", "95%", "99%", "99.9%"
  };
  char buf[512];
  int pos = 0;

  if (cnt == 0) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%-25s: N/A", name);
    printk(KERN_INFO "%s\n", buf);
    return;
  }

  total_ns /= cnt;

  long long total_us_x100 = total_ns / 10;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "%-25s: avg %lld.%02d us",
                name, total_us_x100/100, total_us_x100%100);

  for (int i = 0; i < MAX_DIST_BUCKET; ++i) {
    int us_x100 = dist[i] / 10; // ns → us * 100
    pos += snprintf(buf + pos, sizeof(buf) - pos, ", %s %d.%02d",
                  percentiles[i], us_x100/100, us_x100%100);
  }

  printk(KERN_INFO "%s\n", buf);
}

static inline void print_latency_breakdown_summary(
    const struct latency_breakdown_summary* s) {
  printk(KERN_INFO "=== Latency Breakdown (avg with distribution) ===\n");
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
  printk(KERN_INFO "==================================================\n");
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