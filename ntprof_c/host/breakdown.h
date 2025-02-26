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


static inline void print_breakdown(struct breakdown* b) {
  switch (b->type) {
    case BREAKDOWN_READ:
      print_read_breakdown(&b->read);
      break;
    case BREAKDOWN_WRITES:
      print_write_breakdown_s(&b->writes);
      break;
    case BREAKDOWN_WRITEL:
      print_write_breakdown_l(&b->writel);
      break;
    default:
      pr_err("Unknown breakdown type: %d\n", b->type);
      break;
  }
}

void break_latency_read(struct profile_record* record,
                        struct read_breakdown* breakdown);

void break_latency_write_s(struct profile_record* record,
                           struct write_breakdown_s* breakdown);

void break_latency_write_l(struct profile_record* record,
                           struct write_breakdown_l* breakdown);

#endif //BREAKDOWN_H