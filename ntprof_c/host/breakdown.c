#include "breakdown.h"

#include "analyzer.h"

void break_latency_read(struct profile_record* record,
                        struct latency_breakdown* breakdown) {
  u64 blk_submit = record->timestamps[0];
  u64 nvme_tcp_queue_rq = record->timestamps[1];
  u64 nvme_tcp_done_send_req = record->timestamps[2];
  u64 nvme_tcp_recv_skb = record->timestamps[3];
  u64 nvme_tcp_handle_c2h_data = record->timestamps[4];
  u64 nvme_tcp_process_nvme_cqe = record->timestamps[5];
  u64 blk_rq_complete = record->timestamps[6];

  breakdown->blk_completion = nvme_tcp_queue_rq - blk_submit;
  breakdown->nvme_tcp_submission = nvme_tcp_done_send_req - nvme_tcp_queue_rq;
  int waiting = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
  breakdown->nstack_completion = nvme_tcp_handle_c2h_data - nvme_tcp_recv_skb;
  breakdown->nvme_tcp_completion =
      nvme_tcp_process_nvme_cqe - nvme_tcp_handle_c2h_data;
  breakdown->blk_completion = blk_rq_complete - nvme_tcp_process_nvme_cqe;
}

void break_latency_write(struct profile_record* record,
                         struct latency_breakdown* breakdown) {
  if (record->metadata.contains_r2t) {
    // break down big write
    u64 blk_submit = record->timestamps[0];
    u64 nvme_tcp_queue_rq = record->timestamps[1];
    u64 nvme_tcp_done_send_req = record->timestamps[2];
    u64 nvme_tcp_recv_skb = record->timestamps[3];
    u64 nvme_tcp_handle_r2t = record->timestamps[4];
    u64 nvme_tcp_done_send_req2 = record->timestamps[5];
    u64 nvme_tcp_recv_skb2 = record->timestamps[6];
    u64 nvme_tcp_process_nvme_cqe = record->timestamps[7];
    u64 blk_rq_complete = record->timestamps[8];

    breakdown->blk_completion = nvme_tcp_queue_rq - blk_submit;
    breakdown->nvme_tcp_submission = nvme_tcp_done_send_req - nvme_tcp_queue_rq;
    int waiting1 = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
    breakdown->nstack_completion = nvme_tcp_handle_r2t - nvme_tcp_recv_skb;
    breakdown->nvme_tcp_submission += nvme_tcp_done_send_req2 -
        nvme_tcp_handle_r2t;
    int waiting2 = nvme_tcp_recv_skb2 - nvme_tcp_done_send_req2;
    breakdown->nstack_completion += nvme_tcp_process_nvme_cqe -
        nvme_tcp_recv_skb2;
    breakdown->blk_completion = blk_rq_complete - nvme_tcp_process_nvme_cqe;
  } else {
    // break down small write
    u64 blk_submit = record->timestamps[0];
    u64 nvme_tcp_queue_rq = record->timestamps[1];
    u64 nvme_tcp_done_send_req = record->timestamps[2];
    u64 nvme_tcp_recv_skb = record->timestamps[3];
    u64 nvme_tcp_process_nvme_cqe = record->timestamps[4];
    u64 blk_rq_complete = record->timestamps[5];

    breakdown->blk_completion = nvme_tcp_queue_rq - blk_submit;
    breakdown->nvme_tcp_submission =
        nvme_tcp_done_send_req - nvme_tcp_queue_rq;
    int waiting = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
    breakdown->nstack_completion =
        nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb;
    breakdown->blk_completion = blk_rq_complete - nvme_tcp_process_nvme_cqe;
  }
}

void break_latency(struct profile_record* record,
                   struct latency_breakdown* breakdown) {
  if (record->metadata.is_write) {
    break_latency_write(record, breakdown);
  } else {
    break_latency_read(record, breakdown);
  }
}