// SPDX-License-Identifier: GPL-2.0
/* The stage model on hand-made records with known timestamps. */
#include <string.h>

#include "model.h"
#include "testlib.h"

/* Sum of the main components (details excluded) for one breakdown. */
static int64_t main_sum(const struct ntp_breakdown* b) {
  int64_t sum = 0;

  for (int c = 0; c < NTP_C_NR; c++) {
    if (c == NTP_C_E2E || ntp_comps[c].detail || !ntp_comp_valid(b, c)) continue;
    sum += b->comp[c];
  }
  return sum;
}

static void read_host(struct ntp_host_rec* h) {
  memset(h, 0, sizeof(*h));
  h->hdr.kind = NTP_REC_HOST;
  h->ts[NTP_H_START] = 1000;
  h->ts[NTP_H_QUEUE_RQ] = 1100;    /* blk 100 */
  h->ts[NTP_H_CMD_SENT] = 1300;    /* transport 200 */
  h->ts[NTP_H_C2H_ARR] = 6300;     /* wait 5000 */
  h->ts[NTP_H_C2H_PROC] = 6500;    /* rx 200 */
  h->ts[NTP_H_RSP_ARR] = 6900;
  h->ts[NTP_H_RSP_PROC] = 7000;    /* tr completion 500 */
  h->ts[NTP_H_NVME_DONE] = 7100;
  h->ts[NTP_H_BLK_DONE] = 7300;    /* blk completion 300 */
}

static void read_target(struct ntp_tgt_rec* t) {
  memset(t, 0, sizeof(*t));
  t->hdr.kind = NTP_REC_TARGET;
  /* a different clock: only differences matter */
  t->ts[NTP_T_CMD_ARR] = 50000;
  t->ts[NTP_T_CMD_PROC] = 50100;   /* rx 100 */
  t->ts[NTP_T_EXEC] = 50300;       /* sub 200 */
  t->ts[NTP_T_BE_ISSUE] = 50350;
  t->ts[NTP_T_BE_DONE] = 53350;
  t->ts[NTP_T_COMPLETE] = 53400;   /* backend 3100 */
  t->ts[NTP_T_RSP_QUEUED] = 53400;
  t->ts[NTP_T_C2H_SENT] = 53600;   /* completion 200 */
  t->ts[NTP_T_RSP_SENT] = 53700;   /* residence to first data out: 3600 */
}

static void test_read_joined(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  read_host(&h);
  read_target(&t);
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.joined);
  CHECK_EQ(b.io_class, NTP_IO_CLASS_READ);
  CHECK_EQ(b.comp[NTP_C_BLK_SUB], 100);
  CHECK_EQ(b.comp[NTP_C_TR_SUB], 200);
  CHECK_EQ(b.comp[NTP_C_TGT_RX], 100);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 200);
  CHECK_EQ(b.comp[NTP_C_TGT_BE], 3100);
  CHECK_EQ(b.comp[NTP_C_TGT_CMPL], 200);
  CHECK_EQ(b.comp[NTP_C_NET], 5000 - 3600);
  CHECK_EQ(b.comp[NTP_C_HOST_RX], 200);
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 500);
  CHECK_EQ(b.comp[NTP_C_BLK_CMPL], 300);
  CHECK_EQ(b.comp[NTP_C_E2E], 6300);
  CHECK(!ntp_comp_valid(&b, NTP_C_REMOTE));
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.comp[NTP_C_TGT_DEV], 3000);
  CHECK_EQ(b.comp[NTP_C_TGT_BE_SUB] + b.comp[NTP_C_TGT_DEV] +
               b.comp[NTP_C_TGT_BE_CMPL],
           b.comp[NTP_C_TGT_BE]);
  CHECK_EQ(b.comp[NTP_C_CMPL_IPI], 100);
  /* stages add up too */
  CHECK_EQ(b.stage[NTP_S1] + b.stage[NTP_S2] + b.stage[NTP_S3_S5] +
               b.stage[NTP_S6] + b.stage[NTP_S7_S9],
           b.comp[NTP_C_E2E]);
}

static void test_read_host_only(void) {
  struct ntp_host_rec h;
  struct ntp_breakdown b;

  read_host(&h);
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_OK);
  CHECK(!b.joined);
  CHECK_EQ(b.comp[NTP_C_REMOTE], 5000);
  CHECK(!ntp_comp_valid(&b, NTP_C_NET));
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK(!(b.stage_valid & (1u << NTP_S7_S9)));
  CHECK(b.stage_valid & (1u << NTP_S1));
}

static void test_inline_write(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  memset(&h, 0, sizeof(h));
  h.hdr.flags = NTP_F_WRITE | NTP_F_INLINE_DATA;
  h.ts[NTP_H_START] = 10;
  h.ts[NTP_H_QUEUE_RQ] = 20;
  h.ts[NTP_H_CMD_SENT] = 50;
  h.ts[NTP_H_RSP_ARR] = 950;
  h.ts[NTP_H_RSP_PROC] = 1000;
  h.ts[NTP_H_BLK_DONE] = 1010;
  memset(&t, 0, sizeof(t));
  t.hdr.flags = NTP_F_WRITE | NTP_F_INLINE_DATA;
  t.ts[NTP_T_CMD_ARR] = 100;
  t.ts[NTP_T_CMD_PROC] = 110;
  t.ts[NTP_T_EXEC] = 200;
  t.ts[NTP_T_COMPLETE] = 700;
  t.ts[NTP_T_RSP_SENT] = 720;

  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.io_class, NTP_IO_CLASS_WRITE_INLINE);
  CHECK_EQ(b.comp[NTP_C_NET], 900 - 620);
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 0);
  CHECK_EQ(main_sum(&b), 1000);
  CHECK_EQ(b.comp[NTP_C_E2E], 1000);
}

static void r2t_write(struct ntp_host_rec* h, struct ntp_tgt_rec* t) {
  memset(h, 0, sizeof(*h));
  h->hdr.flags = NTP_F_WRITE | NTP_F_R2T;
  h->ts[NTP_H_START] = 0 + 5;
  h->ts[NTP_H_QUEUE_RQ] = 10;
  h->ts[NTP_H_CMD_SENT] = 20;
  h->ts[NTP_H_R2T_ARR] = 120;
  h->ts[NTP_H_R2T_PROC] = 130;
  h->ts[NTP_H_DATA_START] = 200;
  h->ts[NTP_H_DATA_SENT] = 260; /* informational only */
  h->ts[NTP_H_RSP_ARR] = 900;
  h->ts[NTP_H_RSP_PROC] = 910;
  h->ts[NTP_H_BLK_DONE] = 915;
  memset(t, 0, sizeof(*t));
  t->hdr.flags = NTP_F_WRITE | NTP_F_R2T;
  t->ts[NTP_T_CMD_ARR] = 1000;
  t->ts[NTP_T_CMD_PROC] = 1010;
  t->ts[NTP_T_R2T_QUEUED] = 1015;
  t->ts[NTP_T_R2T_SENT] = 1030;
  t->ts[NTP_T_DATA_ARR] = 1100;
  t->ts[NTP_T_DATA_PROC] = 1110;
  t->ts[NTP_T_EXEC] = 1200;
  t->ts[NTP_T_COMPLETE] = 1700;
  t->ts[NTP_T_RSP_SENT] = 1720;
}

static void test_r2t_write(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  r2t_write(&h, &t);
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.io_class, NTP_IO_CLASS_WRITE_R2T);
  CHECK_EQ(b.comp[NTP_C_TR_SUB], 10 + 70);
  CHECK_EQ(b.comp[NTP_C_HOST_RX], 10 + 10);
  CHECK_EQ(b.comp[NTP_C_TGT_RX], 10 + 10);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 90);
  /* (100 - 30) + (700 - 620) */
  CHECK_EQ(b.comp[NTP_C_NET], 70 + 80);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.comp[NTP_C_E2E], 910);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_WAIT) && !ntp_comp_valid(&b, NTP_C_TGT_DATA));
  CHECK_EQ(b.stage[NTP_S6], 110 + 20);
}

/*
 * A TCP write whose data came in several skbs: from its PDU header parsed
 * (1110) to the end of the last copy (1190) the target copied the data for
 * 50 ns and waited the other 30 for it; the command executes at 1200.
 */
static void test_write_data_phase(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  r2t_write(&h, &t);
  t.ts[NTP_T_DATA_LAST_ARR] = 1170;
  t.ts[NTP_T_DATA_DONE] = 1190;
  t.h2c_busy_ns = 50;
  t.h2c_skbs = 3;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.joined);
  CHECK_EQ(b.comp[NTP_C_TGT_RX], 10 + 10);  /* first data: arrival to parsed */
  CHECK_EQ(b.comp[NTP_C_TGT_DATA], 50);
  CHECK_EQ(b.comp[NTP_C_TGT_WAIT], 30);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 10); /* R2T, then last copy to execute */
  CHECK_EQ(b.comp[NTP_C_NET], 70 + 80);     /* the residence did not change */
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.comp[NTP_C_E2E], 910);
  CHECK_EQ(b.stage[NTP_S6], 30 + 20 + 50);
  CHECK_EQ(b.stage[NTP_S3_S5], 150 + 20 + 20 + 30);
  CHECK_EQ(b.stage[NTP_S1] + b.stage[NTP_S2] + b.stage[NTP_S3_S5] + b.stage[NTP_S6] +
               b.stage[NTP_S7_S9],
           b.comp[NTP_C_E2E]);
  /* the host's side is that of any write after R2T */
  CHECK(!ntp_comp_valid(&b, NTP_C_HOST_WAIT) && !ntp_comp_valid(&b, NTP_C_HOST_DATA));
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 0);

  /* host only: the target's record is what has the split */
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_WAIT) && !ntp_comp_valid(&b, NTP_C_TGT_DATA));
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* the copying never stopped: no wait */
  t.h2c_busy_ns = 80;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_TGT_WAIT], 0);
  CHECK_EQ(b.comp[NTP_C_TGT_DATA], 80);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  /* more than the span cannot be: capped, never a negative wait */
  t.h2c_busy_ns = 100;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_TGT_WAIT], 0);
  CHECK_EQ(b.comp[NTP_C_TGT_DATA], 80);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* executed right after the last copy */
  t.h2c_busy_ns = 50;
  t.ts[NTP_T_DATA_DONE] = t.ts[NTP_T_EXEC];
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20);
  CHECK_EQ(b.comp[NTP_C_TGT_WAIT], 90 - 50);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* a record without the data phase (file version 4, SPDK): as before */
  t.h2c_busy_ns = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_WAIT) && !ntp_comp_valid(&b, NTP_C_TGT_DATA));
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 90);
  CHECK_EQ(b.stage[NTP_S6], 110 + 20);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  /* nor with stamps that do not fit the record */
  t.h2c_busy_ns = 50;
  t.ts[NTP_T_DATA_DONE] = 1250; /* after the command executed */
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_WAIT));
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 90);
  t.ts[NTP_T_DATA_DONE] = 1105; /* before the data's header was parsed */
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_DATA));
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 90);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
}

static void test_incomplete(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  read_host(&h);
  h.ts[NTP_H_C2H_ARR] = 0;
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_INCOMPLETE_HOST);

  read_host(&h);
  read_target(&t);
  t.ts[NTP_T_EXEC] = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_INCOMPLETE_TARGET);
  CHECK(!b.joined);
  CHECK(ntp_comp_valid(&b, NTP_C_REMOTE));
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* a target record of another class is not used */
  read_host(&h);
  read_target(&t);
  t.hdr.flags = NTP_F_WRITE | NTP_F_R2T;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_INCOMPLETE_TARGET);
}

static void test_no_start_time(void) {
  struct ntp_host_rec h;
  struct ntp_breakdown b;

  read_host(&h);
  h.ts[NTP_H_START] = 0;
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_BLK_SUB));
  CHECK_EQ(b.comp[NTP_C_E2E], 7300 - 1100);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
}

/*
 * A TCP read whose data came in several skbs: from the first copy (6500) to
 * the last data processed (6900) the transport worked 120 ns on the data and
 * waited the other 280 for it; the response is processed at 7000.
 */
static void test_read_data_phase(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  read_host(&h);
  read_target(&t);
  h.ts[NTP_H_C2H_LAST_ARR] = 6850;
  h.ts[NTP_H_C2H_DONE] = 6900;
  h.c2h_busy_ns = 120;
  h.c2h_skbs = 3;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.joined);
  CHECK_EQ(b.comp[NTP_C_HOST_RX], 200);   /* first data: arrival to first copy */
  CHECK_EQ(b.comp[NTP_C_HOST_DATA], 120);
  CHECK_EQ(b.comp[NTP_C_HOST_WAIT], 280);
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 100);
  CHECK_EQ(b.comp[NTP_C_NET], 1400);      /* the first byte's network: unchanged */
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.comp[NTP_C_E2E], 6300);
  CHECK_EQ(b.stage[NTP_S2], 200 + 100 + 120);
  CHECK_EQ(b.stage[NTP_S3_S5], 1400 + 100 + 200 + 280);
  CHECK_EQ(b.stage[NTP_S1] + b.stage[NTP_S2] + b.stage[NTP_S3_S5] + b.stage[NTP_S6] +
               b.stage[NTP_S7_S9],
           b.comp[NTP_C_E2E]);

  /* host only: the same split, the target's side in one component */
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_HOST_WAIT], 280);
  CHECK_EQ(b.comp[NTP_C_REMOTE], 5000);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.stage[NTP_S2], 420);

  /* the copying never stopped: no wait */
  h.c2h_busy_ns = 400;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_HOST_WAIT], 0);
  CHECK_EQ(b.comp[NTP_C_HOST_DATA], 400);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  /* more than the span cannot be: capped, never a negative wait */
  h.c2h_busy_ns = 450;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_HOST_WAIT], 0);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* completed inside the call with the last data: no completion left */
  h.c2h_busy_ns = 120;
  h.ts[NTP_H_C2H_DONE] = h.ts[NTP_H_RSP_PROC];
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 0);
  CHECK_EQ(b.comp[NTP_C_HOST_WAIT], 500 - 120);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* a record without the data phase (file version 3, SPDK): as before */
  h.c2h_busy_ns = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_HOST_WAIT) && !ntp_comp_valid(&b, NTP_C_HOST_DATA));
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 500);
  CHECK_EQ(b.stage[NTP_S2], 700);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  /* nor with stamps that do not fit the record */
  h.c2h_busy_ns = 120;
  h.ts[NTP_H_C2H_DONE] = 7200; /* after the response was processed */
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_HOST_WAIT));
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 500);
}

static void rdma_host(struct ntp_host_rec* h, bool write) {
  memset(h, 0, sizeof(*h));
  h->hdr.transport = NTP_TR_RDMA;
  h->hdr.flags = write ? NTP_F_WRITE : 0;
  h->ts[NTP_H_START] = 100;
  h->ts[NTP_H_QUEUE_RQ] = 110;
  h->ts[NTP_H_CMD_SENT] = 140;
  h->ts[NTP_H_SEND_DONE] = 150;
  h->ts[NTP_H_RSP_ARR] = 1140; /* host waits 1000 */
  h->ts[NTP_H_RSP_PROC] = 1160;
  h->ts[NTP_H_BLK_DONE] = 1170;
}

static void test_rdma_read(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  rdma_host(&h, false);
  memset(&t, 0, sizeof(t));
  t.hdr.transport = NTP_TR_RDMA;
  t.ts[NTP_T_CMD_ARR] = 5000;
  t.ts[NTP_T_CMD_PROC] = 5010;
  t.ts[NTP_T_EXEC] = 5030;
  t.ts[NTP_T_COMPLETE] = 5530;
  t.ts[NTP_T_RSP_QUEUED] = 5530;
  t.ts[NTP_T_C2H_SENT] = 5560;
  t.ts[NTP_T_RSP_SENT] = 5560; /* residence 560 */

  /* without the send completion (version 2 records): no data transfer */
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(!ntp_comp_valid(&b, NTP_C_NET_DATA));

  t.ts[NTP_T_SEND_DONE] = 5960; /* WRITE + SEND posted to completion: 400 */
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.joined);
  CHECK_EQ(b.comp[NTP_C_NET_DATA], 400);
  CHECK_EQ(b.io_class, NTP_IO_CLASS_READ);
  CHECK_EQ(b.comp[NTP_C_TR_SUB], 30);
  CHECK_EQ(b.comp[NTP_C_NET], 1000 - 560);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20);
  CHECK_EQ(b.comp[NTP_C_TGT_BE], 500);
  CHECK_EQ(b.comp[NTP_C_TGT_CMPL], 30);
  CHECK_EQ(b.comp[NTP_C_HOST_RX], 20);
  CHECK_EQ(b.comp[NTP_C_TR_CMPL], 0);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  CHECK_EQ(b.comp[NTP_C_E2E], 1070);

  /* host only */
  CHECK_EQ(ntp_breakdown(&h, NULL, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_REMOTE], 1000);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* the posting was not seen: nvmet_req_complete stands in */
  t.ts[NTP_T_RSP_QUEUED] = t.ts[NTP_T_C2H_SENT] = t.ts[NTP_T_RSP_SENT] = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.joined && b.post_estimated);
  CHECK_EQ(b.comp[NTP_C_TGT_CMPL], 0);
  CHECK_EQ(b.comp[NTP_C_NET], 1000 - 530);
  CHECK_EQ(b.comp[NTP_C_NET_DATA], 430);
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* a later command's posting and drive completion in the record: ignored */
  t.ts[NTP_T_C2H_SENT] = t.ts[NTP_T_RSP_SENT] = 9000;
  t.ts[NTP_T_BE_ISSUE] = 5040;
  t.ts[NTP_T_BE_DONE] = 9000;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK(b.post_estimated);
  CHECK_EQ(b.comp[NTP_C_NET], 1000 - 530);
  CHECK(!ntp_comp_valid(&b, NTP_C_TGT_DEV));
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);
  t.ts[NTP_T_BE_DONE] = 5500;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.comp[NTP_C_TGT_DEV], 460);

  /* nor the send completion: the command may not have finished */
  t.ts[NTP_T_C2H_SENT] = t.ts[NTP_T_RSP_SENT] = 0;
  t.ts[NTP_T_SEND_DONE] = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_INCOMPLETE_TARGET);
}

/* A write whose data the target fetched with RDMA READ: the fetch is network. */
static void test_rdma_write_fetch(void) {
  struct ntp_host_rec h;
  struct ntp_tgt_rec t;
  struct ntp_breakdown b;

  rdma_host(&h, true);
  memset(&t, 0, sizeof(t));
  t.hdr.transport = NTP_TR_RDMA;
  t.hdr.flags = NTP_F_WRITE | NTP_F_RDMA_READ;
  t.ts[NTP_T_CMD_ARR] = 0 + 100;
  t.ts[NTP_T_CMD_PROC] = 110;
  t.ts[NTP_T_R2T_SENT] = 130;  /* READ posted */
  t.ts[NTP_T_DATA_ARR] = 430;  /* READ done: fetch 300 */
  t.ts[NTP_T_DATA_PROC] = 430;
  t.ts[NTP_T_EXEC] = 440;
  t.ts[NTP_T_COMPLETE] = 640;
  t.ts[NTP_T_RSP_SENT] = 660;  /* residence 560 - 300 = 260 */

  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_OK);
  CHECK_EQ(b.io_class, NTP_IO_CLASS_WRITE_R2T);
  CHECK_EQ(b.comp[NTP_C_TGT_SUB], 20 + 10);
  CHECK_EQ(b.comp[NTP_C_NET], 1000 - 260);
  CHECK_EQ(b.comp[NTP_C_NET_DATA], 300); /* the RDMA READ */
  CHECK_EQ(main_sum(&b), b.comp[NTP_C_E2E]);

  /* the fetch flag without its events: the target record is not used */
  t.ts[NTP_T_DATA_PROC] = 0;
  CHECK_EQ(ntp_breakdown(&h, &t, &b), NTP_MODEL_INCOMPLETE_TARGET);
  CHECK(ntp_comp_valid(&b, NTP_C_REMOTE));
}

int main(void) {
  RUN(test_rdma_read);
  RUN(test_rdma_write_fetch);
  RUN(test_read_joined);
  RUN(test_read_host_only);
  RUN(test_read_data_phase);
  RUN(test_inline_write);
  RUN(test_r2t_write);
  RUN(test_write_data_phase);
  RUN(test_incomplete);
  RUN(test_no_start_time);
  TEST_DONE();
}
