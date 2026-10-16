// SPDX-License-Identifier: GPL-2.0
/* Record files: the current version round-trips, and older versions still read. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "testlib.h"
#include "trace_file.h"

struct seen {
  struct ntp_host_rec host;
  struct ntp_tgt_rec tgt;
  int nhost, ntgt;
};

static int on_host(const struct ntp_host_rec* h, void* arg) {
  struct seen* s = arg;

  s->host = *h;
  s->nhost++;
  return 0;
}

static int on_tgt(const struct ntp_tgt_rec* t, void* arg) {
  struct seen* s = arg;

  s->tgt = *t;
  s->ntgt++;
  return 0;
}

static const struct ntp_reader_ops ops = {on_host, on_tgt};

static void make_host(struct ntp_host_rec* h) {
  memset(h, 0, sizeof(*h));
  h->hdr.kind = NTP_REC_HOST;
  h->hdr.transport = NTP_TR_TCP;
  h->id.cid = 7;
  h->ts[NTP_H_START] = 100;
  h->ts[NTP_H_BLK_DONE] = 900;
  h->ts[NTP_H_C2H_DONE] = 800;
  h->c2h_busy_ns = 55;
  h->c2h_skbs = 3;
}

static int temp_file(char* path) {
  int fd = mkstemp(path);

  if (fd < 0) return -1;
  close(fd);
  return 0;
}

static void test_round_trip(void) {
  char path[] = "/tmp/ntp_test_XXXXXX";
  struct ntp_file_header hdr, rd;
  struct ntp_writer w;
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct seen s;

  memset(&s, 0, sizeof(s));
  CHECK_EQ(temp_file(path), 0);
  make_host(&h);
  memset(&t, 0, sizeof(t));
  t.hdr.kind = NTP_REC_TARGET;
  t.ts[NTP_T_SEND_DONE] = 42;
  ntp_header_init(&hdr, 1, NTP_TR_TCP, 10);
  CHECK_EQ(ntp_writer_open(&w, path, &hdr), 0);
  CHECK_EQ(ntp_writer_add(&w, &h, sizeof(h)), 0);
  CHECK_EQ(ntp_writer_add(&w, &t, sizeof(t)), 0);
  CHECK_EQ(ntp_writer_close(&w), 0);
  CHECK_EQ(ntp_read_file(path, &rd, &ops, &s), 0);
  CHECK_EQ(rd.version, NTP_FILE_VERSION);
  CHECK_EQ(s.nhost, 1);
  CHECK_EQ(s.ntgt, 1);
  CHECK_EQ(s.host.c2h_busy_ns, 55);
  CHECK_EQ(s.host.c2h_skbs, 3);
  CHECK_EQ(s.host.ts[NTP_H_C2H_DONE], 800);
  CHECK_EQ(s.tgt.ts[NTP_T_SEND_DONE], 42);
  unlink(path);
}

/* Version 3: host records end at ts[NTP_H_SEND_DONE]; the rest reads as 0. */
static void test_version3(void) {
  char path[] = "/tmp/ntp_test_XXXXXX";
  size_t old_host = offsetof(struct ntp_host_rec, ts) + NTP_H_NR_V3 * sizeof(__u64);
  struct ntp_file_header hdr, rd;
  struct ntp_host_rec h;
  struct seen s;
  FILE* f;

  memset(&s, 0, sizeof(s));
  CHECK_EQ(temp_file(path), 0);
  make_host(&h);
  ntp_header_init(&hdr, 1, NTP_TR_TCP, 10);
  hdr.version = 3;
  hdr.host_rec_size = (uint32_t)old_host;
  f = fopen(path, "wb");
  CHECK(f != NULL);
  if (!f) return;
  fwrite(&hdr, sizeof(hdr), 1, f);
  fwrite(&h, old_host, 1, f); /* two records of the old size */
  fwrite(&h, old_host, 1, f);
  fclose(f);
  CHECK_EQ(ntp_read_file(path, &rd, &ops, &s), 0);
  CHECK_EQ(s.nhost, 2);
  CHECK_EQ(s.host.id.cid, 7);
  CHECK_EQ(s.host.ts[NTP_H_BLK_DONE], 900);
  CHECK_EQ(s.host.ts[NTP_H_C2H_DONE], 0);
  CHECK_EQ(s.host.c2h_busy_ns, 0);

  /* a file from a newer ntprof is refused */
  hdr.version = NTP_FILE_VERSION + 1;
  hdr.host_rec_size = sizeof(struct ntp_host_rec);
  f = fopen(path, "wb");
  CHECK(f != NULL);
  if (!f) return;
  fwrite(&hdr, sizeof(hdr), 1, f);
  fclose(f);
  CHECK_EQ(ntp_read_file(path, &rd, &ops, &s), -1);
  unlink(path);
}

int main(void) {
  RUN(test_round_trip);
  RUN(test_version3);
  TEST_DONE();
}
