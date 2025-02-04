#ifndef ANALYZER_H
#define ANALYZER_H

#include "../include/config.h"
#include "host.h"


struct read_breakdown {
    // blk layer
    u64 blk_submission_queueing;
    // nvme-tcp layer
    u64 nvme_tcp_submission_queueing;
    u64 nvme_tcp_request_processing;
    u64 nvme_tcp_waiting;                // from sending the cmd to receiving the first byte of data
    // nvmet-tcp layer
    u64 nvmet_tcp_cmd_capsule_queueing;
    u64 nvmet_tcp_cmd_processing;
    u64 nvmet_tcp_submit_to_blk;
    u64 nvmet_tcp_completion_queueing;
    u64 nvmet_tcp_response_processing;
    // nvme-tcp layer
    u64 nvme_tcp_completion_queueing;
    u64 nvme_tcp_response_processing; // copy data from tcp socket buffer
    // blk layer
    u64 blk_completion_queueing;
    // e2e
    u64 blk_e2e;
    u64 nvme_tcp_e2e;
    u64 nvmet_tcp_e2e;
    u64 networking;
};

struct write_breakdown_s {
    // blk layer
    u64 blk_submission_queueing;
    // nvme-tcp layer
    u64 nvme_tcp_submission_queueing;
    u64 nvme_tcp_request_processing;
    // nvmet-tcp layer
    u64 nvmet_tcp_cmd_capsule_queueing;
    u64 nvmet_tcp_cmd_processing;
    u64 nvmet_tcp_submit_to_blk;
    u64 nvmet_tcp_completion_queueing;
    u64 nvmet_tcp_response_processing;

    // nvme-tcp layer
    u64 nvme_tcp_waiting_for_resp;
    u64 nvme_tcp_completion_queueing;
    // blk layer
    u64 blk_completion_queueing;

    // summary
    u64 blk_e2e;
    u64 nvme_tcp_e2e;
    u64 nvmet_tcp_e2e;
    u64 networking;
};

struct write_breakdown_l {
    // blk layer
    u64 blk_submission_queueing;
    // nvme-tcp layer
    u64 nvme_tcp_cmd_submission_queueing;
    u64 nvme_tcp_cmd_capsule_processing;

    // nvmet-tcp layer
    u64 nvmet_tcp_cmd_capsule_queueing;
    u64 nvmet_tcp_cmd_processing;
    u64 nvmet_tcp_r2t_submission_queueing;
    u64 nvmet_tcp_r2t_resp_processing;
    u64 nvmet_tcp_wait_for_data;
    //nvme-tcp layer
    u64 nvme_tcp_wait_for_r2t;                // from sending the cmd to receiving the first byte of data
    u64 nvme_tcp_r2t_resp_queueing;
    u64 nvme_tcp_data_pdu_submission_queueing;
    u64 nvme_tcp_data_pdu_processing;
    // nvmet-tcp layer
    u64 nvmet_tcp_write_cmd_queueing;
    u64 nvmet_tcp_write_cmd_processing;
    u64 nvmet_tcp_submit_to_blk;
    u64 nvmet_tcp_completion_queueing;
    u64 nvmet_tcp_response_processing;
    // nvme-tcp layer
    u64 nvme_tcp_wait_for_resp;
    u64 nvme_tcp_resp_queueing;
    // blk layer
    u64 blk_completion_queueing;

    // summary
    u64 blk_e2e;
    u64 networking;
};

struct latency_breakdown {

};

void analyze_statistics(struct per_core_statistics *stat, struct ntprof_config *config);

#endif // ANALYZER_H
