/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Task specification. The ini file keeps v1's sections and keys
 * (src/ntprof_config.ini), so a v1 configuration runs unchanged; command-line
 * options override it.
 */
#ifndef NTPROF_CONFIG_H
#define NTPROF_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ntprof.h"

enum ntp_role {
  NTP_ROLE_HOST = 1,
  NTP_ROLE_TARGET = 2,
  NTP_ROLE_BOTH = NTP_ROLE_HOST | NTP_ROLE_TARGET,
};

enum ntp_aggr {
  NTP_AGG_AVG,
  NTP_AGG_MIN,
  NTP_AGG_MAX,
  NTP_AGG_DIST,
};

enum ntp_io_type {
  NTP_IO_READ = 0,
  NTP_IO_WRITE = 1,
  NTP_IO_BOTH = 2,
};

struct ntp_config {
  /* [Workload] */
  char session[64]; /* block device (nvme1n1) or "all" */
  enum ntp_io_type io_type;
  uint32_t sizes[NTP_MAX_SIZE_FILTERS];
  int n_sizes;
  uint64_t min_size; /* 0 = none */
  uint64_t max_size; /* 0 = none */

  /* [Profiling] */
  bool online;
  char data_dir[256];
  unsigned interval_s;
  uint32_t sample_every; /* v1: FREQUENCY */
  unsigned buffer_mb;
  enum ntp_aggr aggr;

  /* [Report] */
  bool rep_distribution;
  bool rep_throughput;
  bool rep_breakdown;
  bool rep_queue_length;
  bool rep_detail;     /* v2: every component, not only the stages */
  bool rep_all_groups; /* v2: also groups with under 1 % of the records */
  bool group_by_size;
  bool group_by_type;
  bool group_by_session;

  /* command line only */
  enum ntp_role role;
  enum ntp_transport transport;
  unsigned duration_s; /* 0 = until interrupted */
  char output[256];    /* record file; empty = none */
  char json[256];      /* JSON report file; empty = none */
  char btf_dir[256];   /* BTF generated from module DWARF; empty = kernel BTF */
  bool exact_queues;   /* per-request queue occupancy (costs per-I/O map ops) */
  bool force_kprobes;  /* kprobes even when fentry/fexit are available */
  /*
   * The role runs in SPDK: comma-separated binaries or libraries carrying
   * the ntprof USDT probes; empty = the kernel driver.
   */
  char spdk_host[512];
  char spdk_target[512];
};

void config_defaults(struct ntp_config* c);

/* Returns 0, or -1 after printing what is wrong. */
int config_load_ini(struct ntp_config* c, const char* path);

/* Option values shared by the ini parser and the command line. */
int config_set_io_type(struct ntp_config* c, const char* v);
int config_set_sizes(struct ntp_config* c, const char* v);
int config_set_aggr(struct ntp_config* c, const char* v);

void config_print(const struct ntp_config* c, FILE* f);

#endif /* NTPROF_CONFIG_H */
