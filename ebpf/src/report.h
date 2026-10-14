/* SPDX-License-Identifier: GPL-2.0 */
/* Text and JSON reports, and the LAD comparison of two runs. */
#ifndef NTPROF_REPORT_H
#define NTPROF_REPORT_H

#include <stdbool.h>
#include <stdio.h>

#include "config.h"
#include "stats.h"
#include "trace_file.h"

struct ntp_report_opts {
  enum ntp_aggr aggr;
  bool distribution; /* quantiles beside the mean */
  bool breakdown;    /* the share of each stage */
  bool detail;       /* every component, as a table */
  bool queue_length; /* commands in each stage (Little's law), exact queues */
  bool all_groups;   /* also groups with under 1 % of the records */
};

void ntp_report_opts_from_config(struct ntp_report_opts* o,
                                 const struct ntp_config* c);

/* Throughput over an interval, from the BPF counters (all commands). */
struct ntp_tput {
  double seconds;
  uint64_t read_ios, read_bytes, write_ios, write_bytes;
  bool estimated; /* scaled up from the sampled commands (SPDK) */
};

void ntp_report_tput(FILE* f, const struct ntp_tput* t);

/*
 * The report of a run: per group (I/O type, size, device), the latency and
 * the share of each stage; every component with o->detail. Groups with under
 * 1 % of the records are listed on one line unless o->all_groups.
 */
void ntp_report_text(FILE* f, struct ntp_stats* s,
                     const struct ntp_report_opts* o);

/* One line per group for an online interval that ended @t_s after the start. */
void ntp_report_interval_header(FILE* f);
void ntp_report_interval(FILE* f, struct ntp_stats* s,
                         const struct ntp_report_opts* o, double t_s);

/* @meta and @tput may be NULL. Returns 0 or -1. */
int ntp_report_json(const char* path, struct ntp_stats* s,
                    const struct ntp_file_header* meta,
                    const struct ntp_tput* tput);

/*
 * LAD of each stage and component: mean under load / mean of the baseline,
 * for every group present in both runs.
 */
void ntp_report_compare(FILE* f, struct ntp_stats* base,
                        struct ntp_stats* loaded,
                        const struct ntp_report_opts* o);

#endif /* NTPROF_REPORT_H */
