/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Aggregation of breakdowns into groups (I/O type, size, device), as v1's
 * analyzer did (src/host/analyzer.c), with the same quantiles.
 */
#ifndef NTPROF_STATS_H
#define NTPROF_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"
#include "ntprof.h"
#include "util.h"

/* v1's quantiles: 10, 50, 70, 80, 90, 95, 99, 99.9 % */
#define NTP_NQ 8
extern const double ntp_quantiles[NTP_NQ];
extern const char* const ntp_quantile_names[NTP_NQ];

struct ntp_summary {
  size_t n;
  double mean;
  int64_t min, max;
  int64_t q[NTP_NQ];
};

/* Sorts @v in place. */
void ntp_summarize(struct i64vec* v, struct ntp_summary* out);

enum ntp_queue_metric {
  NTP_Q_HOST_UNSENT = 0, /* initiator: capsules waiting to be sent      */
  NTP_Q_HOST_INFLIGHT,   /* initiator: commands not yet completed       */
  NTP_Q_TGT_OUTSTANDING, /* target: commands not yet answered           */
  NTP_Q_TGT_RESP_QUEUE,  /* target: responses waiting to be sent        */
  NTP_Q_NR,
};

extern const char* const ntp_queue_names[NTP_Q_NR];

struct ntp_group_key {
  int is_write;   /* -1 = not grouped by type */
  uint32_t bytes; /* 0 = not grouped by size */
  char session[NTP_DISK_NAME_LEN]; /* "" = not grouped by device */
};

struct ntp_group {
  struct ntp_group_key key;
  uint64_t records;      /* host records seen */
  uint64_t joined;       /* with a complete target record */
  uint64_t bad_host;     /* host record missing events */
  uint64_t bad_target;   /* target record missing events */
  uint8_t transport;     /* NTP_TR_* of the host records */
  bool spdk_host;        /* host records come from SPDK */
  bool spdk_target;      /* joined target records come from SPDK */
  struct i64vec comp[NTP_C_NR];
  struct i64vec stage[NTP_STAGE_NR];
  struct i64vec queue[NTP_Q_NR];
};

struct ntp_stats {
  struct ntp_group* groups;
  size_t n, cap;
  bool by_type, by_size, by_session;
  uint32_t sample_every; /* to scale record counts to command counts */
  uint64_t first_ns;     /* earliest queue_rq of the records added */
  uint64_t last_ns;      /* latest completion of the records added */
};

void ntp_stats_init(struct ntp_stats* s, bool by_type, bool by_size,
                    bool by_session);
void ntp_stats_free(struct ntp_stats* s);
/* Forget all samples, keep the grouping. */
void ntp_stats_reset(struct ntp_stats* s);

/*
 * Commands per second of a group: its records, scaled by the sampling rate,
 * over the time the records span. 0 when the span is too short to tell.
 */
double ntp_group_rate(const struct ntp_stats* s, const struct ntp_group* g);

/* Add one host record and its target record (or NULL). */
void ntp_stats_add(struct ntp_stats* s, const struct ntp_host_rec* h,
                   const struct ntp_tgt_rec* t);

/* nvme1c2n3 -> nvme1n3: name the namespace, not the path. */
void ntp_session_name(const char* disk, char* out, size_t len);

/* "read 4KiB nvme1n1" (parts that are grouped on) */
void ntp_group_label(const struct ntp_group_key* k, char* out, size_t len);

#endif /* NTPROF_STATS_H */
