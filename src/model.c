// SPDX-License-Identifier: GPL-2.0
#include "model.h"

#include <string.h>

#include "util.h"

/* In the NSDI'25 paper's terms, in path order: S1(S), S2(S), S3-S5, S5(S),
 * S6(S), S7-S9, S6(C), S3(C), S2(C), S1(C); then S3-S9, S1-S9, S7(S), S8-S9,
 * S7(C), part of S1(C), and the RDMA data transfer within S3-S5. */
const struct ntp_comp_info ntp_comps[NTP_C_NR] = {
    [NTP_C_BLK_SUB] = {"blk_submission", "host_block_layer",
                       "host block layer, submission", false},
    [NTP_C_TR_SUB] = {"transport_submission", "host_driver",
                      "host driver, submission", false},
    [NTP_C_NET] = {"network", "network", "network: wire and NICs (computed)",
                   false},
    [NTP_C_TGT_RX] = {"target_rx_stack", "network", "network: target receive path",
                      false},
    [NTP_C_TGT_SUB] = {"target_transport_submission", "target_driver",
                       "target driver, submission", false},
    [NTP_C_TGT_BE] = {"target_subsystem", "storage",
                      "storage: target block layer and drive", false},
    [NTP_C_TGT_CMPL] = {"target_transport_completion", "target_driver",
                        "target driver, completion", false},
    [NTP_C_HOST_RX] = {"host_rx_stack", "network", "network: host receive path",
                       false},
    [NTP_C_TR_CMPL] = {"transport_completion", "host_driver",
                       "host driver, completion", false},
    [NTP_C_BLK_CMPL] = {"blk_completion", "host_block_layer",
                        "host block layer, completion", false},
    [NTP_C_REMOTE] = {"remote", "network_and_target",
                      "network and target (no target record)", false},
    [NTP_C_E2E] = {"end_to_end", "end_to_end", "end to end", false},
    [NTP_C_TGT_BE_SUB] = {"target_blk_submission", "storage",
                          "target block layer, submission", true},
    [NTP_C_TGT_DEV] = {"target_device", "storage", "drive", true},
    [NTP_C_TGT_BE_CMPL] = {"target_blk_completion", "storage",
                           "target block layer, completion", true},
    [NTP_C_CMPL_IPI] = {"completion_ipi", "host_block_layer",
                        "handoff to nvme_complete_rq", true},
    [NTP_C_NET_DATA] = {"net_data_transfer", "network",
                        "data transfer (RDMA, at the target)", true},
};

const char* const ntp_stage_keys[NTP_STAGE_NR] = {
    [NTP_S1] = "host_block_layer", [NTP_S2] = "host_driver",
    [NTP_S3_S5] = "network",       [NTP_S6] = "target_driver",
    [NTP_S7_S9] = "storage",
};

enum ntp_io_class ntp_io_class_of(const struct ntp_host_rec* h) {
  if (!(h->hdr.flags & NTP_F_WRITE)) return NTP_IO_CLASS_READ;
  if (h->hdr.flags & NTP_F_R2T) return NTP_IO_CLASS_WRITE_R2T;
  return NTP_IO_CLASS_WRITE_INLINE;
}

static bool have_all(const __u64* ts, const int* idx, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (!ts[idx[i]]) return false;
  return true;
}

static const int host_req_read[] = {NTP_H_QUEUE_RQ, NTP_H_CMD_SENT,
                                    NTP_H_C2H_ARR,  NTP_H_C2H_PROC,
                                    NTP_H_RSP_PROC, NTP_H_BLK_DONE};
static const int host_req_inline[] = {NTP_H_QUEUE_RQ, NTP_H_CMD_SENT,
                                      NTP_H_RSP_ARR, NTP_H_RSP_PROC,
                                      NTP_H_BLK_DONE};
static const int host_req_r2t[] = {
    NTP_H_QUEUE_RQ,   NTP_H_CMD_SENT, NTP_H_R2T_ARR,  NTP_H_R2T_PROC,
    NTP_H_DATA_START, NTP_H_RSP_ARR,  NTP_H_RSP_PROC, NTP_H_BLK_DONE};

static const int tgt_req_read[] = {NTP_T_CMD_ARR, NTP_T_CMD_PROC, NTP_T_EXEC,
                                   NTP_T_COMPLETE, NTP_T_C2H_SENT};
static const int tgt_req_inline[] = {NTP_T_CMD_ARR, NTP_T_CMD_PROC, NTP_T_EXEC,
                                     NTP_T_COMPLETE, NTP_T_RSP_SENT};
static const int tgt_req_r2t[] = {
    NTP_T_CMD_ARR,  NTP_T_CMD_PROC, NTP_T_R2T_SENT, NTP_T_DATA_ARR,
    NTP_T_DATA_PROC, NTP_T_EXEC,    NTP_T_COMPLETE, NTP_T_RSP_SENT};

static bool host_complete(const struct ntp_host_rec* h, enum ntp_io_class c) {
  switch (c) {
    case NTP_IO_CLASS_READ:
      return have_all(h->ts, host_req_read, ARRAY_SIZE(host_req_read));
    case NTP_IO_CLASS_WRITE_INLINE:
      return have_all(h->ts, host_req_inline, ARRAY_SIZE(host_req_inline));
    default:
      return have_all(h->ts, host_req_r2t, ARRAY_SIZE(host_req_r2t));
  }
}

static bool target_complete(const struct ntp_tgt_rec* t, enum ntp_io_class c) {
  switch (c) {
    case NTP_IO_CLASS_READ:
      return have_all(t->ts, tgt_req_read, ARRAY_SIZE(tgt_req_read));
    case NTP_IO_CLASS_WRITE_INLINE:
      return have_all(t->ts, tgt_req_inline, ARRAY_SIZE(tgt_req_inline));
    default:
      return have_all(t->ts, tgt_req_r2t, ARRAY_SIZE(tgt_req_r2t));
  }
}

static void set(struct ntp_breakdown* b, enum ntp_comp c, int64_t v) {
  b->comp[c] = v;
  b->valid |= 1u << c;
}

static int64_t d(uint64_t later, uint64_t earlier) {
  return (int64_t)(later - earlier);
}

/* What every transport shares on the host: blk-mq at both ends, submission. */
static void common_host(const struct ntp_host_rec* h, struct ntp_breakdown* b) {
  const __u64* H = h->ts;

  set(b, NTP_C_TR_SUB, d(H[NTP_H_CMD_SENT], H[NTP_H_QUEUE_RQ]));
  set(b, NTP_C_BLK_CMPL, d(H[NTP_H_BLK_DONE], H[NTP_H_RSP_PROC]));
  if (H[NTP_H_START] && H[NTP_H_START] <= H[NTP_H_QUEUE_RQ]) {
    set(b, NTP_C_BLK_SUB, d(H[NTP_H_QUEUE_RQ], H[NTP_H_START]));
    set(b, NTP_C_E2E, d(H[NTP_H_BLK_DONE], H[NTP_H_START]));
  } else {
    set(b, NTP_C_E2E, d(H[NTP_H_BLK_DONE], H[NTP_H_QUEUE_RQ]));
  }
  if (H[NTP_H_NVME_DONE])
    set(b, NTP_C_CMPL_IPI, d(H[NTP_H_NVME_DONE], H[NTP_H_RSP_PROC]));
}

/* The bdev backend's block request splits the target subsystem time. */
static void backend_details(const __u64* T, struct ntp_breakdown* b) {
  /* a drive completion after nvmet_req_complete belongs to another command */
  if (!T[NTP_T_BE_ISSUE] || !T[NTP_T_BE_DONE] || T[NTP_T_BE_DONE] > T[NTP_T_COMPLETE])
    return;
  set(b, NTP_C_TGT_BE_SUB, d(T[NTP_T_BE_ISSUE], T[NTP_T_EXEC]));
  set(b, NTP_C_TGT_DEV, d(T[NTP_T_BE_DONE], T[NTP_T_BE_ISSUE]));
  set(b, NTP_C_TGT_BE_CMPL, d(T[NTP_T_COMPLETE], T[NTP_T_BE_DONE]));
}

/*
 * NVMe/TCP, the host side alone. For each round trip, "wait" runs from the
 * moment the host handed its PDU to TCP to the arrival of the target's answer
 * at the host's network stack; it covers network and target.
 */
static void host_side(const struct ntp_host_rec* h, struct ntp_breakdown* b) {
  const __u64* H = h->ts;

  common_host(h, b);
  switch (b->io_class) {
    case NTP_IO_CLASS_READ:
      set(b, NTP_C_REMOTE, d(H[NTP_H_C2H_ARR], H[NTP_H_CMD_SENT]));
      set(b, NTP_C_HOST_RX, d(H[NTP_H_C2H_PROC], H[NTP_H_C2H_ARR]));
      set(b, NTP_C_TR_CMPL, d(H[NTP_H_RSP_PROC], H[NTP_H_C2H_PROC]));
      break;
    case NTP_IO_CLASS_WRITE_INLINE:
      set(b, NTP_C_REMOTE, d(H[NTP_H_RSP_ARR], H[NTP_H_CMD_SENT]));
      set(b, NTP_C_HOST_RX, d(H[NTP_H_RSP_PROC], H[NTP_H_RSP_ARR]));
      set(b, NTP_C_TR_CMPL, 0);
      break;
    default:
      /*
       * The second round trip starts when the host starts sending the data:
       * the target receives it while the host is still sending, so the
       * transfer is inside both the host's wait and the target's residence.
       */
      b->comp[NTP_C_TR_SUB] += d(H[NTP_H_DATA_START], H[NTP_H_R2T_PROC]);
      set(b, NTP_C_REMOTE, d(H[NTP_H_R2T_ARR], H[NTP_H_CMD_SENT]) +
                               d(H[NTP_H_RSP_ARR], H[NTP_H_DATA_START]));
      set(b, NTP_C_HOST_RX, d(H[NTP_H_R2T_PROC], H[NTP_H_R2T_ARR]) +
                                d(H[NTP_H_RSP_PROC], H[NTP_H_RSP_ARR]));
      set(b, NTP_C_TR_CMPL, 0);
      break;
  }
}

/* Split NTP_C_REMOTE into network and target components. */
static void target_side(const struct ntp_tgt_rec* t, struct ntp_breakdown* b) {
  const __u64* T = t->ts;
  int64_t residence;

  set(b, NTP_C_TGT_BE, d(T[NTP_T_COMPLETE], T[NTP_T_EXEC]));
  switch (b->io_class) {
    case NTP_IO_CLASS_READ:
      set(b, NTP_C_TGT_RX, d(T[NTP_T_CMD_PROC], T[NTP_T_CMD_ARR]));
      set(b, NTP_C_TGT_SUB, d(T[NTP_T_EXEC], T[NTP_T_CMD_PROC]));
      set(b, NTP_C_TGT_CMPL, d(T[NTP_T_C2H_SENT], T[NTP_T_COMPLETE]));
      residence = d(T[NTP_T_C2H_SENT], T[NTP_T_CMD_ARR]);
      break;
    case NTP_IO_CLASS_WRITE_INLINE:
      set(b, NTP_C_TGT_RX, d(T[NTP_T_CMD_PROC], T[NTP_T_CMD_ARR]));
      set(b, NTP_C_TGT_SUB, d(T[NTP_T_EXEC], T[NTP_T_CMD_PROC]));
      set(b, NTP_C_TGT_CMPL, d(T[NTP_T_RSP_SENT], T[NTP_T_COMPLETE]));
      residence = d(T[NTP_T_RSP_SENT], T[NTP_T_CMD_ARR]);
      break;
    default:
      set(b, NTP_C_TGT_RX, d(T[NTP_T_CMD_PROC], T[NTP_T_CMD_ARR]) +
                               d(T[NTP_T_DATA_PROC], T[NTP_T_DATA_ARR]));
      set(b, NTP_C_TGT_SUB, d(T[NTP_T_R2T_SENT], T[NTP_T_CMD_PROC]) +
                                d(T[NTP_T_EXEC], T[NTP_T_DATA_PROC]));
      set(b, NTP_C_TGT_CMPL, d(T[NTP_T_RSP_SENT], T[NTP_T_COMPLETE]));
      residence = d(T[NTP_T_R2T_SENT], T[NTP_T_CMD_ARR]) +
                  d(T[NTP_T_RSP_SENT], T[NTP_T_DATA_ARR]);
      break;
  }
  set(b, NTP_C_NET, b->comp[NTP_C_REMOTE] - residence);
  b->valid &= ~(1u << NTP_C_REMOTE);
  b->comp[NTP_C_REMOTE] = 0;
  backend_details(T, b);
}

/*
 * NVMe/RDMA. The host CPU sees one round trip: the command posted, the
 * response's completion polled; the data moves by RDMA WRITE (reads) or RDMA
 * READ (writes) without it. A write's RDMA READ is wire and remote-NIC time
 * seen from the target, so it is network time, not target time.
 */
static const int host_req_rdma[] = {NTP_H_QUEUE_RQ, NTP_H_CMD_SENT,
                                    NTP_H_RSP_ARR, NTP_H_RSP_PROC,
                                    NTP_H_BLK_DONE};
static const int tgt_req_rdma[] = {NTP_T_CMD_ARR, NTP_T_CMD_PROC, NTP_T_EXEC,
                                   NTP_T_COMPLETE};

static void rdma_host_side(const struct ntp_host_rec* h, struct ntp_breakdown* b) {
  const __u64* H = h->ts;

  common_host(h, b);
  set(b, NTP_C_REMOTE, d(H[NTP_H_RSP_ARR], H[NTP_H_CMD_SENT]));
  set(b, NTP_C_HOST_RX, d(H[NTP_H_RSP_PROC], H[NTP_H_RSP_ARR]));
  set(b, NTP_C_TR_CMPL, 0);
}

static bool rdma_target_side(const struct ntp_tgt_rec* t, struct ntp_breakdown* b) {
  __u64 T[NTP_T_NR];
  bool fetch = t->hdr.flags & NTP_F_RDMA_READ;
  int64_t fetch_ns = 0, residence;

  memcpy(T, t->ts, sizeof(T));
  if (!have_all(T, tgt_req_rdma, ARRAY_SIZE(tgt_req_rdma)) ||
      (fetch && (!T[NTP_T_R2T_SENT] || !T[NTP_T_DATA_PROC])))
    return false;
  /* a posting after its own send completion was another command's */
  if (T[NTP_T_SEND_DONE] && T[NTP_T_RSP_SENT] > T[NTP_T_SEND_DONE])
    T[NTP_T_RSP_SENT] = T[NTP_T_C2H_SENT] = 0;
  if (!T[NTP_T_RSP_SENT]) {
    /*
     * The posting of the response (nvmet_rdma_queue_response()) was not seen,
     * but the command finished: its send completion was. The response is
     * queued right after nvmet_req_complete(), so that time stands in for the
     * posting; the target transport completion then reads 0 (it is under a
     * microsecond when seen).
     */
    if (!T[NTP_T_SEND_DONE]) return false;
    T[NTP_T_RSP_SENT] = T[NTP_T_COMPLETE];
    if (!(t->hdr.flags & NTP_F_WRITE)) T[NTP_T_C2H_SENT] = T[NTP_T_COMPLETE];
    b->post_estimated = true;
  }
  set(b, NTP_C_TGT_RX, d(T[NTP_T_CMD_PROC], T[NTP_T_CMD_ARR]));
  if (fetch) {
    fetch_ns = d(T[NTP_T_DATA_PROC], T[NTP_T_R2T_SENT]);
    set(b, NTP_C_TGT_SUB, d(T[NTP_T_R2T_SENT], T[NTP_T_CMD_PROC]) +
                              d(T[NTP_T_EXEC], T[NTP_T_DATA_PROC]));
    b->io_class = NTP_IO_CLASS_WRITE_R2T;
    /* the RDMA READ: the dpbench pipeline's NIC stage of a NIC-to-SSD write */
    set(b, NTP_C_NET_DATA, fetch_ns);
  } else {
    set(b, NTP_C_TGT_SUB, d(T[NTP_T_EXEC], T[NTP_T_CMD_PROC]));
  }
  /* a read's RDMA WRITE + response, posted until the send completion */
  if (!(t->hdr.flags & NTP_F_WRITE) && T[NTP_T_C2H_SENT] && T[NTP_T_SEND_DONE])
    set(b, NTP_C_NET_DATA, d(T[NTP_T_SEND_DONE], T[NTP_T_C2H_SENT]));
  set(b, NTP_C_TGT_BE, d(T[NTP_T_COMPLETE], T[NTP_T_EXEC]));
  set(b, NTP_C_TGT_CMPL, d(T[NTP_T_RSP_SENT], T[NTP_T_COMPLETE]));
  residence = d(T[NTP_T_RSP_SENT], T[NTP_T_CMD_ARR]) - fetch_ns;
  set(b, NTP_C_NET, b->comp[NTP_C_REMOTE] - residence);
  b->valid &= ~(1u << NTP_C_REMOTE);
  b->comp[NTP_C_REMOTE] = 0;
  backend_details(T, b);
  return true;
}

static void stage(struct ntp_breakdown* b, enum ntp_stage s,
                  const enum ntp_comp* parts, size_t n) {
  int64_t sum = 0;

  for (size_t i = 0; i < n; i++) {
    if (!ntp_comp_valid(b, parts[i])) return;
    sum += b->comp[parts[i]];
  }
  b->stage[s] = sum;
  b->stage_valid |= 1u << s;
}

static void stages(struct ntp_breakdown* b) {
  static const enum ntp_comp s1[] = {NTP_C_BLK_SUB, NTP_C_BLK_CMPL};
  static const enum ntp_comp s2[] = {NTP_C_TR_SUB, NTP_C_TR_CMPL};
  static const enum ntp_comp s3_5[] = {NTP_C_NET, NTP_C_TGT_RX, NTP_C_HOST_RX};
  static const enum ntp_comp s6[] = {NTP_C_TGT_SUB, NTP_C_TGT_CMPL};
  static const enum ntp_comp s7_9[] = {NTP_C_TGT_BE};

  stage(b, NTP_S1, s1, ARRAY_SIZE(s1));
  stage(b, NTP_S2, s2, ARRAY_SIZE(s2));
  stage(b, NTP_S3_S5, s3_5, ARRAY_SIZE(s3_5));
  stage(b, NTP_S6, s6, ARRAY_SIZE(s6));
  stage(b, NTP_S7_S9, s7_9, ARRAY_SIZE(s7_9));
}

enum ntp_model_err ntp_breakdown(const struct ntp_host_rec* h,
                                 const struct ntp_tgt_rec* t,
                                 struct ntp_breakdown* out) {
  enum ntp_model_err err = NTP_MODEL_OK;

  memset(out, 0, sizeof(*out));
  out->io_class = ntp_io_class_of(h);

  if (h->hdr.transport == NTP_TR_RDMA) {
    if (!have_all(h->ts, host_req_rdma, ARRAY_SIZE(host_req_rdma)))
      return NTP_MODEL_INCOMPLETE_HOST;
    rdma_host_side(h, out);
    if (t) {
      bool same_dir = !(t->hdr.flags & NTP_F_WRITE) == !(h->hdr.flags & NTP_F_WRITE);

      if (same_dir && t->hdr.transport == NTP_TR_RDMA && rdma_target_side(t, out))
        out->joined = true;
      else
        err = NTP_MODEL_INCOMPLETE_TARGET;
    }
    stages(out);
    return err;
  }

  if (!host_complete(h, out->io_class)) return NTP_MODEL_INCOMPLETE_HOST;
  host_side(h, out);
  if (t) {
    bool same_class = !!(t->hdr.flags & NTP_F_R2T) ==
                      (out->io_class == NTP_IO_CLASS_WRITE_R2T);

    if (same_class && target_complete(t, out->io_class)) {
      target_side(t, out);
      out->joined = true;
    } else {
      err = NTP_MODEL_INCOMPLETE_TARGET;
    }
  }
  stages(out);
  return err;
}
