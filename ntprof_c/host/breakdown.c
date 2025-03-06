#include "breakdown.h"

#include "analyzer.h"

void break_latency_read(struct profile_record* record,
                        struct latency_breakdown* breakdown) {
    // initiator
    u64 blk_submit               = record->timestamps[0];
    u64 nvme_tcp_queue_rq        = record->timestamps[1];
    u64 nvme_tcp_done_send_req   = record->timestamps[2];
    u64 nvme_tcp_recv_skb        = record->timestamps[3];
    u64 nvme_tcp_handle_c2h_data = record->timestamps[4];
    u64 nvme_tcp_process_nvme_cqe= record->timestamps[5];
    u64 blk_rq_complete          = record->timestamps[6];

    breakdown->blk_submission     = nvme_tcp_queue_rq - blk_submit;
    breakdown->nvme_tcp_submission= nvme_tcp_done_send_req - nvme_tcp_queue_rq;
    int waiting                   = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
    breakdown->nstack_completion  = nvme_tcp_handle_c2h_data - nvme_tcp_recv_skb;
    breakdown->nvme_tcp_completion= nvme_tcp_process_nvme_cqe - nvme_tcp_handle_c2h_data;
    breakdown->blk_completion     = blk_rq_complete - nvme_tcp_process_nvme_cqe;

    // target
    struct ntprof_stat* stat = &record->stat1;
    u64 nvmet_tcp_try_recv_pdu  = stat->ts[0];
    u64 nvmet_tcp_done_recv_pdu = stat->ts[1];
    u64 nvmet_tcp_exec_read      = stat->ts[2];
    u64 nvmet_tcp_queue_response= stat->ts[3];
    u64 nvmet_tcp_send_response = stat->ts[4];

    breakdown->nstack_submission = nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
    breakdown->nvmet_tcp_submission = nvmet_tcp_exec_read - nvmet_tcp_done_recv_pdu;
    breakdown->target_subsystem  = nvmet_tcp_queue_response - nvmet_tcp_exec_read;
    breakdown->nvmet_tcp_completion= nvmet_tcp_send_response - nvmet_tcp_queue_response;

    int target_end2end = nvmet_tcp_send_response - nvmet_tcp_try_recv_pdu;
    breakdown->network_transmission = waiting - target_end2end;
}

void break_latency_write(struct profile_record* record,
                         struct latency_breakdown* breakdown) {
    if (record->metadata.contains_r2t) {
        // initiator
        u64 blk_submit             = record->timestamps[0];
        u64 nvme_tcp_queue_rq      = record->timestamps[1];
        u64 nvme_tcp_done_send_req = record->timestamps[2];
        u64 nvme_tcp_recv_skb      = record->timestamps[3];
        u64 nvme_tcp_handle_r2t    = record->timestamps[4];
        u64 nvme_tcp_done_send_req2= record->timestamps[5];
        u64 nvme_tcp_recv_skb2     = record->timestamps[6];
        u64 nvme_tcp_process_nvme_cqe=record->timestamps[7];
        u64 blk_rq_complete        = record->timestamps[8];

        breakdown->blk_submission  = nvme_tcp_queue_rq - blk_submit;
        breakdown->nvme_tcp_submission = nvme_tcp_done_send_req - nvme_tcp_queue_rq;
        int waiting1              = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
        breakdown->nstack_completion = nvme_tcp_handle_r2t - nvme_tcp_recv_skb;
        breakdown->nvme_tcp_submission += nvme_tcp_done_send_req2 - nvme_tcp_handle_r2t;
        int waiting2              = nvme_tcp_recv_skb2 - nvme_tcp_done_send_req2;
        breakdown->nstack_completion += nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb2;
        breakdown->blk_completion = blk_rq_complete - nvme_tcp_process_nvme_cqe;

        // target
        struct ntprof_stat* stat1 = &record->stat1;
        u64 nvmet_tcp_try_recv_pdu = stat1->ts[0];
        u64 nvmet_tcp_done_recv_pdu= stat1->ts[1];
        u64 nvmet_tcp_try_send_r2t = stat1->ts[2];

        breakdown->nstack_submission = nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
        breakdown->nvmet_tcp_submission = nvmet_tcp_try_send_r2t - nvmet_tcp_done_recv_pdu;

        struct ntprof_stat* stat2 = &record->stat2;
        nvmet_tcp_try_recv_pdu     = stat2->ts[0];
        u64 nvmet_tcp_handle_h2c   = stat2->ts[1];
        u64 nvmet_tcp_exec_write   = stat2->ts[2];
        u64 nvmet_tcp_queue_resp   = stat2->ts[3];
        u64 nvmet_tcp_send_resp    = stat2->ts[4];

        breakdown->nstack_submission += nvmet_tcp_handle_h2c - nvmet_tcp_try_recv_pdu;
        breakdown->nvmet_tcp_submission += nvmet_tcp_exec_write - nvmet_tcp_handle_h2c;
        breakdown->target_subsystem = nvmet_tcp_queue_resp - nvmet_tcp_exec_write;
        breakdown->nvmet_tcp_completion = nvmet_tcp_send_resp - nvmet_tcp_queue_resp;
    } else {
        // initiator
        u64 blk_submit             = record->timestamps[0];
        u64 nvme_tcp_queue_rq      = record->timestamps[1];
        u64 nvme_tcp_done_send_req = record->timestamps[2];
        u64 nvme_tcp_recv_skb      = record->timestamps[3];
        u64 nvme_tcp_process_cqe   = record->timestamps[4];
        u64 blk_rq_complete        = record->timestamps[5];

        breakdown->blk_submission  = nvme_tcp_queue_rq - blk_submit;
        breakdown->nvme_tcp_submission = nvme_tcp_done_send_req - nvme_tcp_queue_rq;
        int waiting               = nvme_tcp_recv_skb - nvme_tcp_done_send_req;
        breakdown->nstack_completion = nvme_tcp_process_cqe - nvme_tcp_recv_skb;
        breakdown->blk_completion = blk_rq_complete - nvme_tcp_process_cqe;

        // target
        struct ntprof_stat* stat = &record->stat1;
        u64 nvmet_tcp_try_recv_pdu = stat->ts[0];
        u64 nvmet_tcp_done_recv_pdu= stat->ts[1];
        u64 nvmet_tcp_exec_write   = stat->ts[2];
        u64 nvmet_tcp_queue_resp   = stat->ts[3];
        u64 nvmet_tcp_send_resp   = stat->ts[4];

        breakdown->nstack_submission = nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
        breakdown->nvmet_tcp_submission= nvmet_tcp_exec_write - nvmet_tcp_done_recv_pdu;
        breakdown->target_subsystem = nvmet_tcp_queue_resp - nvmet_tcp_exec_write;
        breakdown->nvmet_tcp_completion= nvmet_tcp_send_resp - nvmet_tcp_queue_resp;

        int target_end_to_end = nvmet_tcp_send_resp - nvmet_tcp_try_recv_pdu;
        breakdown->network_transmission = waiting - target_end_to_end;
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