// SPDX-License-Identifier: GPL-2.0
/* Quantiles, grouping and device names. */
#include <string.h>

#include "stats.h"
#include "testlib.h"

static void test_quantiles(void) {
  struct i64vec v = {0};
  struct ntp_summary s;

  /* 1..1001 shuffled: quantile q sits at 1 + q * 1000 */
  for (int i = 0; i <= 1000; i++) i64vec_push(&v, (i * 7919) % 1001 + 1);
  ntp_summarize(&v, &s);
  CHECK_EQ(s.n, 1001);
  CHECK_EQ(s.min, 1);
  CHECK_EQ(s.max, 1001);
  CHECK(s.mean > 500.99 && s.mean < 501.01);
  CHECK_EQ(s.q[0], 101);   /* p10 */
  CHECK_EQ(s.q[1], 501);   /* p50 */
  CHECK_EQ(s.q[6], 991);   /* p99 */
  CHECK_EQ(s.q[7], 1000);  /* p99.9 */
  i64vec_free(&v);

  /* interpolation between two samples, rounded */
  i64vec_push(&v, 0);
  i64vec_push(&v, 1000);
  ntp_summarize(&v, &s);
  CHECK_EQ(s.q[1], 500);
  CHECK_EQ(s.q[7], 999);
  i64vec_free(&v);

  ntp_summarize(&v, &s);
  CHECK_EQ(s.n, 0);
}

static void test_session_name(void) {
  char out[32];

  ntp_session_name("nvme3c5n1", out, sizeof(out));
  CHECK(!strcmp(out, "nvme3n1"));
  ntp_session_name("nvme10c12n7", out, sizeof(out));
  CHECK(!strcmp(out, "nvme10n7"));
  ntp_session_name("nvme2n1", out, sizeof(out));
  CHECK(!strcmp(out, "nvme2n1"));
  ntp_session_name("sda", out, sizeof(out));
  CHECK(!strcmp(out, "sda"));
}

static void add(struct ntp_stats* s, bool write, uint32_t bytes,
                const char* disk) {
  struct ntp_host_rec h;

  memset(&h, 0, sizeof(h));
  h.hdr.flags = write ? NTP_F_WRITE | NTP_F_INLINE_DATA : 0;
  h.bytes = bytes;
  snprintf(h.disk, sizeof(h.disk), "%s", disk);
  h.ts[NTP_H_QUEUE_RQ] = 1;
  h.ts[NTP_H_CMD_SENT] = 2;
  h.ts[NTP_H_C2H_ARR] = 3;
  h.ts[NTP_H_C2H_PROC] = 4;
  h.ts[NTP_H_RSP_ARR] = 5;
  h.ts[NTP_H_RSP_PROC] = 6;
  h.ts[NTP_H_BLK_DONE] = 7;
  ntp_stats_add(s, &h, NULL);
}

static void test_grouping(void) {
  struct ntp_stats s;
  char label[64];

  ntp_stats_init(&s, true, true, true);
  add(&s, false, 4096, "nvme1c1n1");
  add(&s, false, 4096, "nvme1c2n1"); /* other path, same namespace */
  add(&s, false, 8192, "nvme1c1n1");
  add(&s, true, 4096, "nvme1c1n1");
  CHECK_EQ(s.n, 3);
  CHECK_EQ(s.groups[0].records, 2);
  CHECK_EQ(s.groups[2].records, 1);
  CHECK_EQ(s.groups[2].comp[NTP_C_E2E].n, 1);
  ntp_group_label(&s.groups[0].key, label, sizeof(label));
  CHECK(!strcmp(label, "read 4KiB nvme1n1"));
  ntp_group_label(&s.groups[1].key, label, sizeof(label));
  CHECK(!strcmp(label, "read 8KiB nvme1n1"));
  s.groups[1].key.bytes = 3 << 20;
  ntp_group_label(&s.groups[1].key, label, sizeof(label));
  CHECK(!strcmp(label, "read 3MiB nvme1n1"));
  s.groups[1].key.bytes = 4608;
  ntp_group_label(&s.groups[1].key, label, sizeof(label));
  CHECK(!strcmp(label, "read 4608B nvme1n1"));
  ntp_stats_free(&s);

  ntp_stats_init(&s, false, false, false);
  add(&s, false, 4096, "a");
  add(&s, true, 8192, "b");
  CHECK_EQ(s.n, 1);
  ntp_group_label(&s.groups[0].key, label, sizeof(label));
  CHECK(!strcmp(label, "all"));
  ntp_stats_reset(&s);
  CHECK_EQ(s.n, 0);
  CHECK(!s.by_type);
  ntp_stats_free(&s);
}

int main(void) {
  RUN(test_quantiles);
  RUN(test_session_name);
  RUN(test_grouping);
  TEST_DONE();
}
