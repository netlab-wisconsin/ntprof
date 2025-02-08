/*
 *#include "breakdown.h"

struct event_mapping {
    enum EEvent event;
    unsigned long long *var;
    int *counter; // 用于需要计数的字段
    size_t var_offset; // 多实例的偏移量（一般暂用不到时可为0）
};

#define EVENT_MAP(_event, _var, _cnt) \
    { .event = (_event), .var = &(_var), .counter = _cnt, .var_offset = 0 }

static void process_events(struct ts_entry *ts,
                           const struct event_mapping *maps,
                           size_t num_maps) {
    struct ts_entry *entry;
    const struct event_mapping *m;

    pr_info("process_events is called");

    list_for_each_entry(entry, &ts->list, list) {
        pr_info("current event is %s", event_to_string(entry->event));
        for (m = maps; m < &maps[num_maps]; ++m) {
            if (entry->event != m->event)
                continue;

            if (m->counter) {
                int cnt = (*m->counter)++;
                if (cnt == 0) {
                    *(m->var) = entry->timestamp;
                } else if (m->var_offset) {
                    *(unsigned long long *) ((char *) m->var + m->var_offset) = entry->timestamp;
                }
            } else {
                *(m->var) = entry->timestamp;
                pr_info("assigned %llu to a variable", entry->timestamp);
            }
            break;
        }
    }
}

void break_latency_read(struct profile_record *record, struct read_breakdown *breakdown) {
    // local variables, to record timestamps in record
    unsigned long long blk_submit = 0ULL;
    unsigned long long blk_complete = 0ULL;
    unsigned long long nvme_tcp_queue_rq = 0ULL;
    unsigned long long nvme_tcp_queue_request = 0ULL;
    unsigned long long nvme_tcp_try_send = 0ULL;
    unsigned long long nvme_tcp_try_send_cmd_pdu = 0ULL;
    unsigned long long nvme_tcp_done_send_req = 0ULL;
    unsigned long long nvme_tcp_recv_skb1 = 0ULL;
    unsigned long long nvme_tcp_recv_skb2 = 0ULL;
    unsigned long long nvme_tcp_handle_c2h_data = 0ULL;
    unsigned long long nvme_tcp_recv_data = 0ULL;
    unsigned long long nvme_tcp_process_nvme_cqe = 0ULL;
    unsigned long long nvmet_tcp_try_recv_pdu = 0ULL;
    unsigned long long nvmet_tcp_done_recv_pdu = 0ULL;
    unsigned long long nvmet_tcp_exec_read_req = 0ULL;
    unsigned long long nvmet_tcp_queue_response = 0ULL;
    unsigned long long nvmet_tcp_setup_c2h_data_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_data_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_data = 0ULL;
    unsigned long long nvmet_tcp_setup_response_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_response = 0ULL;

    // counter for duplicated events
    int nvme_tcp_recv_skb_cnt = 0;

    // offset for duplicated events
    size_t offset_nvme_tcp_recv_skb =
            (size_t) ((char *) &nvme_tcp_recv_skb2 - (char *) &nvme_tcp_recv_skb1);

    // map the event to the local variables
    struct event_mapping maps[] = {
        {BLK_SUBMIT, &blk_submit, NULL, 0},
        {BLK_RQ_COMPLETE, &blk_complete, NULL, 0},
        {NVME_TCP_QUEUE_RQ, &nvme_tcp_queue_rq, NULL, 0},
        {NVME_TCP_QUEUE_REQUEST, &nvme_tcp_queue_request, NULL, 0},
        {NVME_TCP_TRY_SEND, &nvme_tcp_try_send, NULL, 0},
        {NVME_TCP_TRY_SEND_CMD_PDU, &nvme_tcp_try_send_cmd_pdu, NULL, 0},
        {NVME_TCP_DONE_SEND_REQ, &nvme_tcp_done_send_req, NULL, 0},
        {NVME_TCP_RECV_SKB, &nvme_tcp_recv_skb1, &nvme_tcp_recv_skb_cnt, offset_nvme_tcp_recv_skb},
        {NVME_TCP_HANDLE_C2H_DATA, &nvme_tcp_handle_c2h_data, NULL, 0},
        {NVME_TCP_RECV_DATA, &nvme_tcp_recv_data, NULL, 0},
        {NVME_TCP_PROCESS_NVME_CQE, &nvme_tcp_process_nvme_cqe, NULL, 0},

        {NVMET_TCP_TRY_RECV_PDU, &nvmet_tcp_try_recv_pdu, NULL, 0},
        {NVMET_TCP_DONE_RECV_PDU, &nvmet_tcp_done_recv_pdu, NULL, 0},
        {NVMET_TCP_EXEC_READ_REQ, &nvmet_tcp_exec_read_req, NULL, 0},
        {NVMET_TCP_QUEUE_RESPONSE, &nvmet_tcp_queue_response, NULL, 0},
        {NVMET_TCP_SETUP_C2H_DATA_PDU, &nvmet_tcp_setup_c2h_data_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_DATA_PDU, &nvmet_tcp_try_send_data_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_DATA, &nvmet_tcp_try_send_data, NULL, 0},
        {NVMET_TCP_SETUP_RESPONSE_PDU, &nvmet_tcp_setup_response_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_RESPONSE, &nvmet_tcp_try_send_response, NULL, 0},
    };

    // copy the value from list to the local variables
    process_events(record->ts, maps, ARRAY_SIZE(maps));

    // calculate latency
    breakdown->cnt++;

    // blk layer
    breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;

    // nvme-tcp layer
    breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send - nvme_tcp_queue_request;
    breakdown->nvme_tcp_request_processing += nvme_tcp_try_send_cmd_pdu - nvme_tcp_try_send;
    // nvmet-tcp layer
    breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
    breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_read_req - nvmet_tcp_done_recv_pdu;
    breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response - nvmet_tcp_exec_read_req;
    breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_c2h_data_pdu - nvmet_tcp_queue_response;
    breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response - nvmet_tcp_setup_c2h_data_pdu;
    // nvme-tcp layer
    breakdown->nvme_tcp_waiting += nvme_tcp_recv_skb1 - nvme_tcp_done_send_req;
    breakdown->nvme_tcp_completion_queueing += nvme_tcp_recv_data - nvme_tcp_recv_skb1;
    breakdown->nvme_tcp_response_processing += nvme_tcp_process_nvme_cqe - nvme_tcp_handle_c2h_data;
    // blk layer
    breakdown->blk_completion_queueing += blk_complete - nvme_tcp_process_nvme_cqe;

    // end to end latency
    breakdown->blk_e2e += (blk_complete - blk_submit);
    breakdown->nvme_tcp_e2e += (nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq);
    breakdown->nvmet_tcp_e2e += (nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu);

    // transmission time
    breakdown->networking += (breakdown->nvme_tcp_waiting - breakdown->nvmet_tcp_e2e);
}

void break_latency_write_s(struct profile_record *record, struct write_breakdown_s *breakdown) {
    unsigned long long blk_submit = 0ULL;
    unsigned long long blk_complete = 0ULL;

    unsigned long long nvme_tcp_queue_rq = 0ULL;
    unsigned long long nvme_tcp_queue_request = 0ULL;
    unsigned long long nvme_tcp_try_send = 0ULL;
    unsigned long long nvme_tcp_try_send_cmd_pdu = 0ULL;
    unsigned long long nvme_tcp_try_send_data = 0ULL;
    unsigned long long nvme_tcp_done_send_req = 0ULL;

    unsigned long long nvmet_tcp_try_recv_pdu = 0ULL;
    unsigned long long nvmet_tcp_done_recv_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_recv_data = 0ULL;
    unsigned long long nvmet_tcp_exec_write_req = 0ULL;
    unsigned long long nvmet_tcp_queue_response = 0ULL;
    unsigned long long nvmet_tcp_setup_response_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_response = 0ULL;

    unsigned long long nvme_tcp_recv_skb = 0ULL;
    unsigned long long nvme_tcp_process_nvme_cqe = 0ULL;


    struct event_mapping maps[] = {
        {BLK_SUBMIT, &blk_submit, NULL, 0},
        {BLK_RQ_COMPLETE, &blk_complete, NULL, 0},
        {NVME_TCP_QUEUE_RQ, &nvme_tcp_queue_rq, NULL, 0},
        {NVME_TCP_QUEUE_REQUEST, &nvme_tcp_queue_request, NULL, 0},
        {NVME_TCP_TRY_SEND, &nvme_tcp_try_send, NULL, 0},
        {NVME_TCP_TRY_SEND_CMD_PDU, &nvme_tcp_try_send_cmd_pdu, NULL, 0},
        {NVME_TCP_TRY_SEND_DATA, &nvme_tcp_try_send_data, NULL, 0},
        {NVME_TCP_DONE_SEND_REQ, &nvme_tcp_done_send_req, NULL, 0},
        {NVMET_TCP_TRY_RECV_PDU, &nvmet_tcp_try_recv_pdu, NULL, 0},
        {NVMET_TCP_DONE_RECV_PDU, &nvmet_tcp_done_recv_pdu, NULL, 0},
        {NVMET_TCP_TRY_RECV_DATA, &nvmet_tcp_try_recv_data, NULL, 0},
        {NVMET_TCP_EXEC_WRITE_REQ, &nvmet_tcp_exec_write_req, NULL, 0},
        {NVMET_TCP_QUEUE_RESPONSE, &nvmet_tcp_queue_response, NULL, 0},
        {NVMET_TCP_SETUP_RESPONSE_PDU, &nvmet_tcp_setup_response_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_RESPONSE, &nvmet_tcp_try_send_response, NULL, 0},
        {NVME_TCP_RECV_SKB, &nvme_tcp_recv_skb, NULL, 0},
        {NVME_TCP_PROCESS_NVME_CQE, &nvme_tcp_process_nvme_cqe, NULL, 0},
    };

    process_events(record->ts, maps, ARRAY_SIZE(maps));

    breakdown->cnt++;

    breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;

    breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send - nvme_tcp_queue_request;
    breakdown->nvme_tcp_request_processing += nvme_tcp_done_send_req - nvme_tcp_try_send;

    breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu;
    breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_write_req - nvmet_tcp_done_recv_pdu;
    breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response - nvmet_tcp_exec_write_req;
    breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_response_pdu - nvmet_tcp_queue_response;
    breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response - nvmet_tcp_setup_response_pdu;

    breakdown->nvme_tcp_waiting_for_resp += nvme_tcp_recv_skb - nvme_tcp_done_send_req;
    breakdown->nvme_tcp_completion_queueing += nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb;

    breakdown->blk_completion_queueing += blk_complete - nvme_tcp_process_nvme_cqe;

    breakdown->blk_e2e += (blk_complete - blk_submit);
    breakdown->nvme_tcp_e2e += (nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq);
    breakdown->nvmet_tcp_e2e += (nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu);

    breakdown->networking += (breakdown->nvme_tcp_waiting_for_resp
                              - breakdown->nvmet_tcp_e2e);
}


void break_latency_write_l(struct profile_record *record, struct write_breakdown_l *breakdown) {
    unsigned long long blk_submit = 0ULL;
    unsigned long long blk_rq_complete = 0ULL;
    unsigned long long nvme_tcp_queue_rq = 0ULL;
    unsigned long long nvme_tcp_queue_request1 = 0ULL, nvme_tcp_queue_request2 = 0ULL;
    unsigned long long nvme_tcp_try_send1 = 0ULL, nvme_tcp_try_send2 = 0ULL;
    unsigned long long nvme_tcp_try_send_cmd_pdu = 0ULL;
    unsigned long long nvme_tcp_done_send_req1 = 0ULL, nvme_tcp_done_send_req2 = 0ULL;
    unsigned long long nvmet_tcp_try_recv_pdu1 = 0ULL, nvmet_tcp_try_recv_pdu2 = 0ULL;
    unsigned long long nvmet_tcp_done_recv_pdu = 0ULL;
    unsigned long long nvmet_tcp_queue_response1 = 0ULL, nvmet_tcp_queue_response2 = 0ULL;
    unsigned long long nvmet_tcp_setup_r2t_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_r2t = 0ULL;
    unsigned long long nvme_tcp_recv_skb1 = 0ULL, nvme_tcp_recv_skb2 = 0ULL;
    unsigned long long nvme_tcp_handle_r2t = 0ULL;
    unsigned long long nvme_tcp_try_send_data_pdu = 0ULL;
    unsigned long long nvme_tcp_try_send_data = 0ULL;
    unsigned long long nvmet_tcp_handle_h2c_data_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_recv_data = 0ULL;
    unsigned long long nvmet_tcp_exec_write_req = 0ULL;
    unsigned long long nvmet_tcp_setup_response_pdu = 0ULL;
    unsigned long long nvmet_tcp_try_send_response = 0ULL;
    unsigned long long nvme_tcp_process_nvme_cqe = 0ULL;


    int nvme_tcp_queue_request_cnt = 0;
    int nvme_tcp_try_send_cnt = 0;
    int nvme_tcp_done_send_req_cnt = 0;
    int nvmet_tcp_try_recv_pdu_cnt = 0;
    int nvmet_tcp_queue_response_cnt = 0;
    int nvme_tcp_recv_skb_cnt = 0;

    size_t off_qreq = (size_t) ((char *) &nvme_tcp_queue_request2 - (char *) &nvme_tcp_queue_request1);
    size_t off_try_send = (size_t) ((char *) &nvme_tcp_try_send2 - (char *) &nvme_tcp_try_send1);
    size_t off_done_send_req = (size_t) ((char *) &nvme_tcp_done_send_req2 - (char *) &nvme_tcp_done_send_req1);
    size_t off_try_recv_pdu = (size_t) ((char *) &nvmet_tcp_try_recv_pdu2 - (char *) &nvmet_tcp_try_recv_pdu1);
    size_t off_queue_response = (size_t) ((char *) &nvmet_tcp_queue_response2 - (char *) &nvmet_tcp_queue_response1);
    size_t off_recv_skb = (size_t) ((char *) &nvme_tcp_recv_skb2 - (char *) &nvme_tcp_recv_skb1);

    struct event_mapping maps[] = {
        {BLK_SUBMIT, &blk_submit, NULL, 0},
        {BLK_RQ_COMPLETE, &blk_rq_complete, NULL, 0},
        {NVME_TCP_QUEUE_RQ, &nvme_tcp_queue_rq, NULL, 0},
        {NVME_TCP_TRY_SEND_CMD_PDU, &nvme_tcp_try_send_cmd_pdu, NULL, 0},
        {NVMET_TCP_DONE_RECV_PDU, &nvmet_tcp_done_recv_pdu, NULL, 0},
        {NVMET_TCP_SETUP_R2T_PDU, &nvmet_tcp_setup_r2t_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_R2T, &nvmet_tcp_try_send_r2t, NULL, 0},
        {NVME_TCP_HANDLE_R2T, &nvme_tcp_handle_r2t, NULL, 0},
        {NVME_TCP_TRY_SEND_DATA_PDU, &nvme_tcp_try_send_data_pdu, NULL, 0},
        {NVME_TCP_TRY_SEND_DATA, &nvme_tcp_try_send_data, NULL, 0},
        {NVMET_TCP_HANDLE_H2C_DATA_PDU, &nvmet_tcp_handle_h2c_data_pdu,NULL, 0},
        {NVMET_TCP_TRY_RECV_DATA, &nvmet_tcp_try_recv_data, NULL, 0},
        {NVMET_TCP_EXEC_WRITE_REQ, &nvmet_tcp_exec_write_req, NULL, 0},
        {NVMET_TCP_SETUP_RESPONSE_PDU, &nvmet_tcp_setup_response_pdu, NULL, 0},
        {NVMET_TCP_TRY_SEND_RESPONSE, &nvmet_tcp_try_send_response, NULL, 0},
        {NVME_TCP_PROCESS_NVME_CQE, &nvme_tcp_process_nvme_cqe, NULL, 0},

        {NVME_TCP_QUEUE_REQUEST, &nvme_tcp_queue_request1, &nvme_tcp_queue_request_cnt, off_qreq},
        {NVME_TCP_TRY_SEND, &nvme_tcp_try_send1, &nvme_tcp_try_send_cnt, off_try_send},
        {NVME_TCP_DONE_SEND_REQ, &nvme_tcp_done_send_req1, &nvme_tcp_done_send_req_cnt, off_done_send_req},
        {NVMET_TCP_TRY_RECV_PDU, &nvmet_tcp_try_recv_pdu1, &nvmet_tcp_try_recv_pdu_cnt, off_try_recv_pdu},
        {NVMET_TCP_QUEUE_RESPONSE, &nvmet_tcp_queue_response1, &nvmet_tcp_queue_response_cnt, off_queue_response},
        {NVME_TCP_RECV_SKB, &nvme_tcp_recv_skb1, &nvme_tcp_recv_skb_cnt, off_recv_skb},
    };

    process_events(record->ts, maps, ARRAY_SIZE(maps));

    breakdown->cnt++;

    breakdown->blk_submission_queueing += (nvme_tcp_queue_rq - blk_submit);
    pr_info("breakdown->blk_submission_queueing is assigned to %llu", breakdown->blk_submission_queueing);

    breakdown->nvme_tcp_cmd_submission_queueing += (nvme_tcp_try_send1 - nvme_tcp_queue_request1);
    breakdown->nvme_tcp_cmd_capsule_processing += (nvme_tcp_try_send_cmd_pdu - nvme_tcp_try_send1);

    breakdown->nvmet_tcp_cmd_capsule_queueing += (nvmet_tcp_done_recv_pdu - nvmet_tcp_try_recv_pdu1);
    breakdown->nvmet_tcp_cmd_processing += (nvmet_tcp_queue_response1 - nvmet_tcp_done_recv_pdu);
    breakdown->nvmet_tcp_r2t_submission_queueing += (nvmet_tcp_setup_r2t_pdu - nvmet_tcp_queue_response1);
    breakdown->nvmet_tcp_r2t_resp_processing += (nvmet_tcp_try_send_r2t - nvmet_tcp_setup_r2t_pdu);

    breakdown->nvme_tcp_wait_for_r2t += (nvme_tcp_recv_skb1 - nvme_tcp_try_send_cmd_pdu);
    breakdown->nvme_tcp_r2t_resp_queueing += (nvme_tcp_handle_r2t - nvme_tcp_recv_skb1);
    breakdown->nvme_tcp_data_pdu_submission_queueing += (nvme_tcp_try_send_data_pdu - nvme_tcp_handle_r2t);
    breakdown->nvme_tcp_data_pdu_processing += (nvme_tcp_done_send_req2 - nvme_tcp_try_send_data);

    breakdown->nvmet_tcp_wait_for_data += (nvmet_tcp_try_recv_pdu2 - nvmet_tcp_try_send_r2t);
    breakdown->nvmet_tcp_write_cmd_queueing += (nvmet_tcp_handle_h2c_data_pdu - nvmet_tcp_try_recv_pdu2);
    breakdown->nvmet_tcp_write_cmd_processing += (nvmet_tcp_exec_write_req - nvmet_tcp_handle_h2c_data_pdu);
    breakdown->nvmet_tcp_submit_to_blk += (nvmet_tcp_queue_response2 - nvmet_tcp_exec_write_req);
    breakdown->nvmet_tcp_completion_queueing += (nvmet_tcp_setup_response_pdu - nvmet_tcp_queue_response2);
    breakdown->nvmet_tcp_response_processing += (nvmet_tcp_try_send_response - nvmet_tcp_setup_response_pdu);

    breakdown->nvme_tcp_wait_for_resp += (nvme_tcp_recv_skb2 - nvme_tcp_done_send_req2);
    breakdown->nvme_tcp_resp_queueing += (nvme_tcp_process_nvme_cqe - nvme_tcp_recv_skb2);

    breakdown->blk_completion_queueing += (blk_rq_complete - nvme_tcp_process_nvme_cqe);

    breakdown->blk_e2e += (blk_rq_complete - blk_submit);

    breakdown->networking += breakdown->nvme_tcp_wait_for_r2t
            + breakdown->nvme_tcp_wait_for_resp
            - (nvmet_tcp_try_send_r2t - nvmet_tcp_try_recv_pdu1)
            - (nvmet_tcp_try_send_response - nvmet_tcp_try_recv_pdu2);
}
*/