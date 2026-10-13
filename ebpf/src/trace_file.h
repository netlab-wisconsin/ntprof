/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Record files ("*.ntp"): a fixed header followed by raw host and target
 * records as the BPF programs emitted them. A file from the initiator and one
 * from the target of the same run can be joined later with `ntprof report`.
 */
#ifndef NTPROF_TRACE_FILE_H
#define NTPROF_TRACE_FILE_H

#include <stdint.h>
#include <stdio.h>

#include "ntprof.h"

#define NTP_FILE_MAGIC "NTPROF2"
#define NTP_FILE_VERSION 2 /* 2: NTP_H_SEND_DONE added (records of 3dd367a and earlier: 1) */

struct ntp_file_header {
  char magic[8];
  uint32_t version;
  uint32_t header_size;
  uint32_t host_rec_size;
  uint32_t tgt_rec_size;
  uint32_t role;         /* enum ntp_role of the recorder */
  uint32_t transport;    /* enum ntp_transport */
  uint32_t sample_every;
  uint32_t pad;
  uint64_t start_mono_ns; /* CLOCK_MONOTONIC when recording started */
  uint64_t start_real_ns; /* CLOCK_REALTIME at the same moment */
  char hostname[64];
  char kernel[64];
};

struct ntp_writer {
  FILE* f;
  uint64_t records;
};

int ntp_writer_open(struct ntp_writer* w, const char* path,
                    const struct ntp_file_header* hdr);
int ntp_writer_add(struct ntp_writer* w, const void* rec, size_t size);
int ntp_writer_close(struct ntp_writer* w);

/* Fill the parts of a header that describe this machine and now. */
void ntp_header_init(struct ntp_file_header* hdr, uint32_t role,
                     uint32_t transport, uint32_t sample_every);

/* Callbacks get each record in file order; a non-zero return stops reading. */
struct ntp_reader_ops {
  int (*host)(const struct ntp_host_rec* h, void* arg);
  int (*target)(const struct ntp_tgt_rec* t, void* arg);
};

/* Returns 0, or -1 after printing what is wrong. */
int ntp_read_file(const char* path, struct ntp_file_header* hdr,
                  const struct ntp_reader_ops* ops, void* arg);

#endif /* NTPROF_TRACE_FILE_H */
