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
  bool distribution;
  bool breakdown;
  bool queue_length;
};

void ntp_report_opts_from_config(struct ntp_report_opts* o,
                                 const struct ntp_config* c);

/* Throughput over an interval, from the BPF counters (all commands). */
struct ntp_tput {
  double seconds;
  uint64_t read_ios, read_bytes, write_ios, write_bytes;
};

void ntp_report_tput(FILE* f, const struct ntp_tput* t);

void ntp_report_text(FILE* f, struct ntp_stats* s,
                     const struct ntp_report_opts* o);

/* @meta and @tput may be NULL. Returns 0 or -1. */
int ntp_report_json(const char* path, struct ntp_stats* s,
                    const struct ntp_file_header* meta,
                    const struct ntp_tput* tput);

/*
 * LAD of each stage and component: mean under load / mean of the baseline,
 * for every group present in both runs.
 */
void ntp_report_compare(FILE* f, struct ntp_stats* base,
                        struct ntp_stats* loaded);

#endif /* NTPROF_REPORT_H */
