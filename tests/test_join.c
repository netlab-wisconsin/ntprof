// SPDX-License-Identifier: GPL-2.0
/* Joining host and target records. */
#include <string.h>

#include "join.h"
#include "testlib.h"

static struct ntp_tgt_rec tgt(uint16_t cntlid, uint16_t qid, uint16_t cid,
                              uint64_t slba, uint64_t arr) {
  struct ntp_tgt_rec t;

  memset(&t, 0, sizeof(t));
  t.hdr.kind = NTP_REC_TARGET;
  t.id.cntlid = cntlid;
  t.id.qid = qid;
  t.id.cid = cid;
  t.id.slba = slba;
  t.id.nsid = 1;
  t.id.nlb = 7;
  t.id.opcode = NTP_NVME_CMD_READ;
  t.ts[NTP_T_CMD_ARR] = arr;
  return t;
}

static struct ntp_host_rec host(uint16_t cntlid, uint16_t qid, uint16_t cid,
                                uint64_t slba) {
  struct ntp_host_rec h;

  memset(&h, 0, sizeof(h));
  h.hdr.kind = NTP_REC_HOST;
  h.id.cntlid = cntlid;
  h.id.qid = qid;
  h.id.cid = cid;
  h.id.slba = slba;
  h.id.nsid = 1;
  h.id.nlb = 7;
  h.id.opcode = NTP_NVME_CMD_READ;
  return h;
}

static void test_match_by_key_and_lba(void) {
  struct ntp_joiner* j = ntp_joiner_new(8);
  struct ntp_tgt_rec t, out;
  struct ntp_host_rec h;

  t = tgt(1, 2, 3, 100, 10);
  ntp_joiner_add_target(j, &t);
  t = tgt(1, 2, 3, 200, 20); /* same cid, other LBA (tag reuse) */
  ntp_joiner_add_target(j, &t);
  t = tgt(1, 3, 3, 100, 30); /* other queue */
  ntp_joiner_add_target(j, &t);
  CHECK_EQ(ntp_joiner_pending(j), 3);

  h = host(1, 2, 3, 200);
  CHECK(ntp_joiner_match(j, &h, &out));
  CHECK_EQ(out.ts[NTP_T_CMD_ARR], 20);
  h = host(1, 2, 3, 100);
  CHECK(ntp_joiner_match(j, &h, &out));
  CHECK_EQ(out.ts[NTP_T_CMD_ARR], 10);
  CHECK(!ntp_joiner_match(j, &h, &out)); /* consumed */
  h = host(2, 3, 3, 100);                 /* other controller */
  CHECK(!ntp_joiner_match(j, &h, &out));
  CHECK_EQ(ntp_joiner_pending(j), 1);
  CHECK_EQ(ntp_joiner_stats(j)->matched, 2);
  CHECK_EQ(ntp_joiner_stats(j)->host_unmatched, 2);
  ntp_joiner_free(j);
}

static void test_fifo_and_eviction(void) {
  struct ntp_joiner* j = ntp_joiner_new(4);
  struct ntp_tgt_rec t, out;
  struct ntp_host_rec h = host(1, 1, 1, 5);

  /* six identical commands: the two oldest are evicted */
  for (int i = 0; i < 6; i++) {
    t = tgt(1, 1, 1, 5, 100 + i);
    ntp_joiner_add_target(j, &t);
  }
  CHECK_EQ(ntp_joiner_pending(j), 4);
  CHECK_EQ(ntp_joiner_stats(j)->targets_evicted, 2);
  CHECK(ntp_joiner_match(j, &h, &out));
  CHECK_EQ(out.ts[NTP_T_CMD_ARR], 102); /* oldest kept */
  CHECK(ntp_joiner_match(j, &h, &out));
  CHECK_EQ(out.ts[NTP_T_CMD_ARR], 103);
  ntp_joiner_free(j);
}

static void test_expire(void) {
  struct ntp_joiner* j = ntp_joiner_new(8);
  struct ntp_tgt_rec t, out;
  struct ntp_host_rec h;

  for (uint16_t cid = 0; cid < 100; cid++) {
    t = tgt(1, 1, cid, cid, cid * 10);
    ntp_joiner_add_target(j, &t);
  }
  CHECK_EQ(ntp_joiner_expire(j, 500), 50);
  CHECK_EQ(ntp_joiner_pending(j), 50);
  h = host(1, 1, 10, 10);
  CHECK(!ntp_joiner_match(j, &h, &out));
  h = host(1, 1, 60, 60);
  CHECK(ntp_joiner_match(j, &h, &out));
  ntp_joiner_free(j);
}

static int released;
static uint64_t released_slba;

static void on_release(void* ctx, const struct ntp_host_rec* h) {
  released++;
  released_slba = h->id.slba;
}

/* Online: a host record that comes before its target record waits for it. */
static void test_host_first(void) {
  struct ntp_joiner* j = ntp_joiner_new(8);
  struct ntp_host_rec h, hout;
  struct ntp_tgt_rec t, tout;

  /* target first: joined as the host arrives */
  t = tgt(1, 2, 3, 100, 10);
  CHECK(!ntp_joiner_target_arrives(j, &t, &hout));
  h = host(1, 2, 3, 100);
  CHECK(ntp_joiner_host_arrives(j, &h, &tout));
  CHECK_EQ(tout.ts[NTP_T_CMD_ARR], 10);

  /* host first: joined as the target arrives */
  h = host(1, 2, 4, 300);
  h.ts[NTP_H_BLK_DONE] = 1000;
  CHECK(!ntp_joiner_host_arrives(j, &h, &tout));
  CHECK_EQ(ntp_joiner_waiting_hosts(j), 1);
  t = tgt(1, 2, 4, 999, 20); /* same key, another command */
  CHECK(!ntp_joiner_target_arrives(j, &t, &hout));
  t = tgt(1, 2, 4, 300, 30);
  CHECK(ntp_joiner_target_arrives(j, &t, &hout));
  CHECK_EQ(hout.id.slba, 300);
  CHECK_EQ(hout.ts[NTP_H_BLK_DONE], 1000);
  CHECK_EQ(ntp_joiner_waiting_hosts(j), 0);
  CHECK_EQ(ntp_joiner_pending(j), 1); /* the slba 999 target */
  CHECK_EQ(ntp_joiner_stats(j)->matched, 2);

  /* a host whose target never comes is released once old enough */
  h = host(1, 2, 5, 500);
  h.ts[NTP_H_BLK_DONE] = 2000;
  CHECK(!ntp_joiner_host_arrives(j, &h, &tout));
  CHECK_EQ(ntp_joiner_release_hosts(j, 2000, on_release, NULL), 0);
  CHECK_EQ(ntp_joiner_release_hosts(j, 2001, on_release, NULL), 1);
  CHECK_EQ(released, 1);
  CHECK_EQ(released_slba, 500);
  CHECK_EQ(ntp_joiner_stats(j)->host_unmatched, 1);
  CHECK_EQ(ntp_joiner_waiting_hosts(j), 0);
  ntp_joiner_free(j);
}

int main(void) {
  RUN(test_match_by_key_and_lba);
  RUN(test_fifo_and_eviction);
  RUN(test_expire);
  RUN(test_host_first);
  TEST_DONE();
}
