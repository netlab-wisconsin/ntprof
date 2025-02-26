#include "breakdown.h"

void break_latency_read(struct profile_record* record,
                        struct read_breakdown* breakdown) {
  struct ts_entry* entry = record->ts;
  unsigned long long blk_submit = 0,
                     blk_complete = 0,
                     nvme_tcp_queue_rq = 0,
                     nvme_tcp_queue_request = 0,
                     nvme_tcp_try_send = 0,
                     nvme_tcp_try_send_cmd_pdu = 0,
                     nvme_tcp_done_send_req = 0,
                     nvme_tcp_recv_skb1 = 0,
                     nvme_tcp_recv_skb2 = 0,
                     nvme_tcp_handle_c2h_data = 0,
                     nvme_tcp_recv_data = 0,
                     nvme_tcp_process_nvme_cqe = 0,
                     nvmet_tcp_try_recv_pdu = 0,
                     nvmet_tcp_done_recv_pdu = 0,
                     nvmet_tcp_exec_read_req = 0,
                     nvmet_tcp_queue_response = 0,
                     nvmet_tcp_setup_c2h_data_pdu = 0,
                     nvmet_tcp_try_send_data_pdu = 0,
                     nvmet_tcp_try_send_data = 0,
                     nvmet_tcp_setup_response_pdu = 0,
                     nvmet_tcp_try_send_response = 0;

  // Traverse the timestamp list and map events to variables
  int nvme_tcp_recv_skb_cnt = 0;
  list_for_each_entry(entry, &record->ts->list, list) {
    switch (entry->event) {
      case BLK_SUBMIT:
        blk_submit = entry->timestamp;
        break;
      case BLK_RQ_COMPLETE:
        blk_complete = entry->timestamp;
        break;
      case NVME_TCP_QUEUE_RQ:
        nvme_tcp_queue_rq = entry->timestamp;
        break;
      case NVME_TCP_QUEUE_REQUEST:
        nvme_tcp_queue_request = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND:
        nvme_tcp_try_send = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND_CMD_PDU:
        nvme_tcp_try_send_cmd_pdu = entry->timestamp;
        break;
      case NVME_TCP_DONE_SEND_REQ:
        nvme_tcp_done_send_req = entry->timestamp;
        break;
      case NVME_TCP_RECV_SKB:
        if (nvme_tcp_recv_skb_cnt == 0) {
          nvme_tcp_recv_skb1 = entry->timestamp;
        } else {
          nvme_tcp_recv_skb2 = entry->timestamp;
        }
        nvme_tcp_recv_skb_cnt++;
        break;
      case NVME_TCP_HANDLE_C2H_DATA:
        nvme_tcp_handle_c2h_data = entry->timestamp;
        break;
      case NVME_TCP_RECV_DATA:
        nvme_tcp_recv_data = entry->timestamp;
        break;
      case NVME_TCP_PROCESS_NVME_CQE:
        nvme_tcp_process_nvme_cqe = entry->timestamp;
        break;
      case NVMET_TCP_TRY_RECV_PDU:
        nvmet_tcp_try_recv_pdu = entry->timestamp;
        break;
      case NVMET_TCP_DONE_RECV_PDU:
        nvmet_tcp_done_recv_pdu = entry->timestamp;
        break;
      case NVMET_TCP_EXEC_READ_REQ:
        nvmet_tcp_exec_read_req = entry->timestamp;
        break;
      case NVMET_TCP_QUEUE_RESPONSE:
        nvmet_tcp_queue_response = entry->timestamp;
        break;
      case NVMET_TCP_SETUP_C2H_DATA_PDU:
        nvmet_tcp_setup_c2h_data_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_DATA_PDU:
        nvmet_tcp_try_send_data_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_DATA:
        nvmet_tcp_try_send_data = entry->timestamp;
        break;
      case NVMET_TCP_SETUP_RESPONSE_PDU:
        nvmet_tcp_setup_response_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_RESPONSE:
        nvmet_tcp_try_send_response = entry->timestamp;
        break;
    }
  }

  breakdown->cnt++;
  // Calculate breakdown latencies
  breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;

  breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send -
      nvme_tcp_queue_request;
  breakdown->nvme_tcp_request_processing += nvme_tcp_try_send_cmd_pdu -
      nvme_tcp_try_send;

  breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu -
      nvmet_tcp_try_recv_pdu;
  breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_read_req -
      nvmet_tcp_done_recv_pdu;
  breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response -
      nvmet_tcp_exec_read_req;
  breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_c2h_data_pdu -
      nvmet_tcp_queue_response;
  breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response -
      nvmet_tcp_setup_c2h_data_pdu;

  breakdown->nvme_tcp_waiting += nvme_tcp_recv_skb1 - nvme_tcp_done_send_req;
  breakdown->nvme_tcp_completion_queueing += nvme_tcp_recv_data -
      nvme_tcp_recv_skb1;
  breakdown->nvme_tcp_response_processing += nvme_tcp_process_nvme_cqe -
      nvme_tcp_handle_c2h_data;

  breakdown->blk_completion_queueing += blk_complete -
      nvme_tcp_process_nvme_cqe;

  // End-to-end latencies
  breakdown->blk_e2e += blk_complete - blk_submit;
  breakdown->nvme_tcp_e2e += nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq;
  breakdown->nvmet_tcp_e2e += nvmet_tcp_try_send_response -
      nvmet_tcp_try_recv_pdu;

  // Networking time
  breakdown->networking += breakdown->nvme_tcp_waiting - breakdown->
      nvmet_tcp_e2e;
}

void break_latency_write_s(struct profile_record* record,
                           struct write_breakdown_s* breakdown) {
  struct ts_entry* entry = record->ts;
  unsigned long long blk_submit = 0,
                     blk_complete = 0,
                     nvme_tcp_queue_rq = 0,
                     nvme_tcp_queue_request = 0,
                     nvme_tcp_try_send = 0,
                     nvme_tcp_try_send_cmd_pdu = 0,
                     nvme_tcp_try_send_data = 0,
                     nvme_tcp_done_send_req = 0,
                     nvmet_tcp_try_recv_pdu = 0,
                     nvmet_tcp_done_recv_pdu = 0,
                     nvmet_tcp_try_recv_data = 0,
                     nvmet_tcp_exec_write_req = 0,
                     nvmet_tcp_queue_response = 0,
                     nvmet_tcp_setup_response_pdu = 0,
                     nvmet_tcp_try_send_response = 0,
                     nvme_tcp_recv_skb = 0,
                     nvme_tcp_process_nvme_cqe = 0;

  // Traverse the timestamp list and map events to variables
  list_for_each_entry(entry, &record->ts->list, list) {
    switch (entry->event) {
      case BLK_SUBMIT:
        blk_submit = entry->timestamp;
        break;
      case BLK_RQ_COMPLETE:
        blk_complete = entry->timestamp;
        break;
      case NVME_TCP_QUEUE_RQ:
        nvme_tcp_queue_rq = entry->timestamp;
        break;
      case NVME_TCP_QUEUE_REQUEST:
        nvme_tcp_queue_request = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND:
        nvme_tcp_try_send = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND_CMD_PDU:
        nvme_tcp_try_send_cmd_pdu = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND_DATA:
        nvme_tcp_try_send_data = entry->timestamp;
        break;
      case NVME_TCP_DONE_SEND_REQ:
        nvme_tcp_done_send_req = entry->timestamp;
        break;
      case NVMET_TCP_TRY_RECV_PDU:
        nvmet_tcp_try_recv_pdu = entry->timestamp;
        break;
      case NVMET_TCP_DONE_RECV_PDU:
        nvmet_tcp_done_recv_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_RECV_DATA:
        nvmet_tcp_try_recv_data = entry->timestamp;
        break;
      case NVMET_TCP_EXEC_WRITE_REQ:
        nvmet_tcp_exec_write_req = entry->timestamp;
        break;
      case NVMET_TCP_QUEUE_RESPONSE:
        nvmet_tcp_queue_response = entry->timestamp;
        break;
      case NVMET_TCP_SETUP_RESPONSE_PDU:
        nvmet_tcp_setup_response_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_RESPONSE:
        nvmet_tcp_try_send_response = entry->timestamp;
        break;
      case NVME_TCP_RECV_SKB:
        nvme_tcp_recv_skb = entry->timestamp;
        break;
      case NVME_TCP_PROCESS_NVME_CQE:
        nvme_tcp_process_nvme_cqe = entry->timestamp;
        break;
    }
  }

  breakdown->cnt++;
  // Calculate breakdown latencies
  breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;

  breakdown->nvme_tcp_submission_queueing += nvme_tcp_try_send -
      nvme_tcp_queue_request;
  breakdown->nvme_tcp_request_processing += nvme_tcp_done_send_req -
      nvme_tcp_try_send;

  breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu -
      nvmet_tcp_try_recv_pdu;
  breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_exec_write_req -
      nvmet_tcp_done_recv_pdu;
  breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response -
      nvmet_tcp_exec_write_req;
  breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_response_pdu -
      nvmet_tcp_queue_response;
  breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response -
      nvmet_tcp_setup_response_pdu;

  breakdown->nvme_tcp_waiting_for_resp += nvme_tcp_recv_skb -
      nvme_tcp_done_send_req;
  breakdown->nvme_tcp_completion_queueing += nvme_tcp_process_nvme_cqe -
      nvme_tcp_recv_skb;

  breakdown->blk_completion_queueing += blk_complete -
      nvme_tcp_process_nvme_cqe;

  // End-to-end latencies
  breakdown->blk_e2e += blk_complete - blk_submit;
  breakdown->nvme_tcp_e2e += nvme_tcp_process_nvme_cqe - nvme_tcp_queue_rq;
  breakdown->nvmet_tcp_e2e += nvmet_tcp_try_send_response -
      nvmet_tcp_try_recv_pdu;

  // Networking time
  breakdown->networking += breakdown->nvme_tcp_waiting_for_resp - breakdown->
      nvmet_tcp_e2e;
}

void break_latency_write_l(struct profile_record* record,
                           struct write_breakdown_l* breakdown) {
  struct ts_entry* entry = record->ts;
  unsigned long long blk_submit = 0,

                     nvme_tcp_queue_rq = 0,
                     nvme_tcp_queue_request1 = 0,
                     nvme_tcp_try_send1 = 0,
                     nvme_tcp_try_send_cmd_pdu = 0,
                     nvme_tcp_done_send_req1 = 0,

                     nvmet_tcp_try_recv_pdu1 = 0,
                     nvmet_tcp_done_recv_pdu = 0,
                     nvmet_tcp_queue_response1 = 0,
                     nvmet_tcp_setup_r2t_pdu = 0,
                     nvmet_tcp_try_send_r2t = 0,

                     nvme_tcp_recv_skb1 = 0,
                     nvme_tcp_handle_r2t = 0,
                     nvme_tcp_queue_request2 = 0,
                     nvme_tcp_try_send2 = 0,
                     nvme_tcp_try_send_data_pdu = 0,
                     nvme_tcp_try_send_data = 0,
                     nvme_tcp_done_send_req2 = 0,

                     nvmet_tcp_try_recv_pdu2 = 0,
                     nvmet_tcp_handle_h2c_data_pdu = 0,
                     nvmet_tcp_try_recv_data = 0,
                     nvmet_tcp_exec_write_req = 0,
                     nvmet_tcp_queue_response2 = 0,
                     nvmet_tcp_setup_response_pdu = 0,
                     nvmet_tcp_try_send_response = 0,

                     nvme_tcp_recv_skb2 = 0,
                     nvme_tcp_process_nvme_cqe = 0,

                     blk_rq_complete = 0;

  int nvme_tcp_queue_request_cnt = 0;
  int nvme_tcp_try_send_cnt = 0;
  int nvme_tcp_done_send_req_cnt = 0;
  int nvmet_tcp_try_recv_pdu_cnt = 0;
  int nvmet_tcp_queue_response_cnt = 0;
  int nvme_tcp_recv_skb_cnt = 0;

  // Traverse the timestamp list and map events to variables
  list_for_each_entry(entry, &record->ts->list, list) {
    switch (entry->event) {
      case BLK_SUBMIT:
        blk_submit = entry->timestamp;
        break;
      case BLK_RQ_COMPLETE:
        blk_rq_complete = entry->timestamp;
        break;
      case NVME_TCP_QUEUE_RQ:
        nvme_tcp_queue_rq = entry->timestamp;
        break;

      case NVME_TCP_QUEUE_REQUEST:
        if (nvme_tcp_queue_request_cnt == 0) {
          nvme_tcp_queue_request1 = entry->timestamp;
        } else {
          nvme_tcp_queue_request2 = entry->timestamp;
        }
        nvme_tcp_queue_request_cnt++;
        break;

      case NVME_TCP_TRY_SEND:
        if (nvme_tcp_try_send_cnt == 0) {
          nvme_tcp_try_send1 = entry->timestamp;
        } else {
          nvme_tcp_try_send2 = entry->timestamp;
        }
        nvme_tcp_try_send_cnt++;
        break;

      case NVME_TCP_TRY_SEND_CMD_PDU:
        nvme_tcp_try_send_cmd_pdu = entry->timestamp;
        break;

      case NVME_TCP_DONE_SEND_REQ:
        if (nvme_tcp_done_send_req_cnt == 0) {
          nvme_tcp_done_send_req1 = entry->timestamp;
        } else {
          nvme_tcp_done_send_req2 = entry->timestamp;
        }
        nvme_tcp_done_send_req_cnt++;
        break;

      case NVMET_TCP_TRY_RECV_PDU:
        if (nvmet_tcp_try_recv_pdu_cnt == 0) {
          nvmet_tcp_try_recv_pdu1 = entry->timestamp;
        } else {
          nvmet_tcp_try_recv_pdu2 = entry->timestamp;
        }
        nvmet_tcp_try_recv_pdu_cnt++;
        break;

      case NVMET_TCP_DONE_RECV_PDU:
        nvmet_tcp_done_recv_pdu = entry->timestamp;
        break;

      case NVMET_TCP_QUEUE_RESPONSE:
        if (nvmet_tcp_queue_response_cnt == 0) {
          nvmet_tcp_queue_response1 = entry->timestamp;
        } else {
          nvmet_tcp_queue_response2 = entry->timestamp;
        }
        nvmet_tcp_queue_response_cnt++;
        break;

      case NVMET_TCP_SETUP_R2T_PDU:
        nvmet_tcp_setup_r2t_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_R2T:
        nvmet_tcp_try_send_r2t = entry->timestamp;
        break;
      case NVME_TCP_HANDLE_R2T:
        nvme_tcp_handle_r2t = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND_DATA_PDU:
        nvme_tcp_try_send_data_pdu = entry->timestamp;
        break;
      case NVME_TCP_TRY_SEND_DATA:
        nvme_tcp_try_send_data = entry->timestamp;
        break;
      case NVMET_TCP_HANDLE_H2C_DATA_PDU:
        nvmet_tcp_handle_h2c_data_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_RECV_DATA:
        nvmet_tcp_try_recv_data = entry->timestamp;
        break;
      case NVMET_TCP_EXEC_WRITE_REQ:
        nvmet_tcp_exec_write_req = entry->timestamp;
        break;
      case NVMET_TCP_SETUP_RESPONSE_PDU:
        nvmet_tcp_setup_response_pdu = entry->timestamp;
        break;
      case NVMET_TCP_TRY_SEND_RESPONSE:
        nvmet_tcp_try_send_response = entry->timestamp;
        break;
      case NVME_TCP_RECV_SKB:
        if (nvme_tcp_recv_skb_cnt == 0) {
          nvme_tcp_recv_skb1 = entry->timestamp;
        } else {
          nvme_tcp_recv_skb2 = entry->timestamp;
        }
        nvme_tcp_recv_skb_cnt++;
        break;
      case NVME_TCP_PROCESS_NVME_CQE:
        nvme_tcp_process_nvme_cqe = entry->timestamp;
        break;
    }
  }
  breakdown->cnt++;
  // Calculate breakdown latencies
  breakdown->blk_submission_queueing += nvme_tcp_queue_rq - blk_submit;

  breakdown->nvme_tcp_cmd_submission_queueing += nvme_tcp_try_send1 -
      nvme_tcp_queue_request1;
  breakdown->nvme_tcp_cmd_capsule_processing += nvme_tcp_try_send_cmd_pdu -
      nvme_tcp_try_send1;

  breakdown->nvmet_tcp_cmd_capsule_queueing += nvmet_tcp_done_recv_pdu -
      nvmet_tcp_try_recv_pdu1;
  breakdown->nvmet_tcp_cmd_processing += nvmet_tcp_queue_response1 -
      nvmet_tcp_done_recv_pdu;
  breakdown->nvmet_tcp_r2t_submission_queueing += nvmet_tcp_setup_r2t_pdu -
      nvmet_tcp_queue_response1;
  breakdown->nvmet_tcp_r2t_resp_processing += nvmet_tcp_try_send_r2t -
      nvmet_tcp_setup_r2t_pdu;

  breakdown->nvme_tcp_wait_for_r2t += nvme_tcp_recv_skb1 -
      nvme_tcp_try_send_cmd_pdu;
  breakdown->nvme_tcp_r2t_resp_queueing += nvme_tcp_handle_r2t -
      nvme_tcp_recv_skb1;
  breakdown->nvme_tcp_data_pdu_submission_queueing += nvme_tcp_try_send_data_pdu
      - nvme_tcp_handle_r2t;
  breakdown->nvme_tcp_data_pdu_processing += nvme_tcp_done_send_req2 -
      nvme_tcp_try_send_data;

  breakdown->nvmet_tcp_wait_for_data += nvmet_tcp_try_recv_pdu2 -
      nvmet_tcp_try_send_r2t;
  breakdown->nvmet_tcp_write_cmd_queueing += nvmet_tcp_handle_h2c_data_pdu -
      nvmet_tcp_try_recv_pdu2;
  breakdown->nvmet_tcp_write_cmd_processing += nvmet_tcp_exec_write_req -
      nvmet_tcp_handle_h2c_data_pdu;
  breakdown->nvmet_tcp_submit_to_blk += nvmet_tcp_queue_response2 -
      nvmet_tcp_exec_write_req;
  // pr_info("nvmet_tcp_submit_to_blk=%llu-%llu=%llu", nvmet_tcp_queue_response2, nvmet_tcp_exec_write_req, nvmet_tcp_queue_response2 - nvmet_tcp_exec_write_req);
  breakdown->nvmet_tcp_completion_queueing += nvmet_tcp_setup_response_pdu -
      nvmet_tcp_queue_response2;
  breakdown->nvmet_tcp_response_processing += nvmet_tcp_try_send_response -
      nvmet_tcp_setup_response_pdu;

  breakdown->nvme_tcp_wait_for_resp += nvme_tcp_recv_skb2 -
      nvme_tcp_done_send_req2;
  breakdown->nvme_tcp_resp_queueing += nvme_tcp_process_nvme_cqe -
      nvme_tcp_recv_skb2;

  breakdown->blk_completion_queueing += blk_rq_complete -
      nvme_tcp_process_nvme_cqe;

  // End-to-end latencies
  breakdown->blk_e2e += blk_rq_complete - blk_submit;
  breakdown->networking += 0; // TODO
}