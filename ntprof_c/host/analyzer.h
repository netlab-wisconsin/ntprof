#ifndef ANALYZER_H
#define ANALYZER_H

#include "../include/config.h"
#include "host.h"
#include "breakdown.h"


void analyze(struct ntprof_config* conf, struct report* rpt);


// struct read_breakdown {
//     u64 cnt;
//     // blk layer
//     u64 blk_submission_queueing;
//     // nvme-tcp layer
//     u64 nvme_tcp_submission_queueing;
//     u64 nvme_tcp_request_processing;
//     u64 nvme_tcp_waiting; // from sending the cmd to receiving the first byte of data
//     // nvmet-tcp layer
//     u64 nvmet_tcp_cmd_capsule_queueing;
//     u64 nvmet_tcp_cmd_processing;
//     u64 nvmet_tcp_submit_to_blk;
//     u64 nvmet_tcp_completion_queueing;
//     u64 nvmet_tcp_response_processing;
//     // nvme-tcp layer
//     u64 nvme_tcp_completion_queueing;
//     u64 nvme_tcp_response_processing; // copy data from tcp socket buffer
//     // blk layer
//     u64 blk_completion_queueing;
//     // e2e
//     u64 blk_e2e;
//     u64 nvme_tcp_e2e;
//     u64 nvmet_tcp_e2e;
//     u64 networking;
// };

// static inline void init_read_breakdown(struct read_breakdown *b) {
//     // initialize all elements to 0
//     memset(b, 0, sizeof(struct read_breakdown));
// }

// static inline void print_read_breakdown(struct read_breakdown *b) {
//     pr_info("latency breakdown: \n");
//     pr_info("blk_submission_queueing: %lu\n", b->blk_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_submission_queueing: %lu\n", b->nvme_tcp_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_request_processing: %lu\n", b->nvme_tcp_request_processing/b->cnt/1000);
//     pr_info("nvme_tcp_waiting: %lu\n", b->nvme_tcp_waiting/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_capsule_queueing: %lu\n", b->nvmet_tcp_cmd_capsule_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_processing: %lu\n", b->nvmet_tcp_cmd_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_submit_to_blk: %lu\n", b->nvmet_tcp_submit_to_blk/b->cnt/1000);
//     pr_info("nvmet_tcp_completion_queueing: %lu\n", b->nvmet_tcp_completion_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_response_processing: %lu\n", b->nvmet_tcp_response_processing/b->cnt/1000);
//     pr_info("nvme_tcp_completion_queueing: %lu\n", b->nvme_tcp_completion_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_response_processing: %lu\n", b->nvme_tcp_response_processing/b->cnt/1000);
//     pr_info("blk_completion_queueing: %lu\n", b->blk_completion_queueing/b->cnt/1000);
//     pr_info("blk_e2e: %lu\n", b->blk_e2e/b->cnt/1000);
//     pr_info("nvme_tcp_e2e: %lu\n", b->nvme_tcp_e2e/b->cnt/1000);
//     pr_info("nvmet_tcp_e2e: %lu\n", b->nvmet_tcp_e2e/b->cnt/1000);
//     pr_info("networking: %lu\n", b->networking/b->cnt/1000);
// }

// struct write_breakdown_s {
//     u64 cnt;
//     // blk layer
//     u64 blk_submission_queueing;
//     // nvme-tcp layer
//     u64 nvme_tcp_submission_queueing;
//     u64 nvme_tcp_request_processing;
//     // nvmet-tcp layer
//     u64 nvmet_tcp_cmd_capsule_queueing;
//     u64 nvmet_tcp_cmd_processing;
//     u64 nvmet_tcp_submit_to_blk;
//     u64 nvmet_tcp_completion_queueing;
//     u64 nvmet_tcp_response_processing;
//
//     // nvme-tcp layer
//     u64 nvme_tcp_waiting_for_resp;
//     u64 nvme_tcp_completion_queueing;
//     // blk layer
//     u64 blk_completion_queueing;
//
//     // summary
//     u64 blk_e2e;
//     u64 nvme_tcp_e2e;
//     u64 nvmet_tcp_e2e;
//     u64 networking;
// };
//
// static inline void init_write_breakdown_s(struct write_breakdown_s *b) {
//     // initialize all elements to 0
//     memset(b, 0, sizeof(struct write_breakdown_s));
// }
//
// static inline void print_write_breakdown_s(struct write_breakdown_s *b) {
//     pr_info("latency breakdown: \n");
//     pr_info("blk_submission_queueing: %lu\n", b->blk_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_submission_queueing: %lu\n", b->nvme_tcp_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_request_processing: %lu\n", b->nvme_tcp_request_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_capsule_queueing: %lu\n", b->nvmet_tcp_cmd_capsule_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_processing: %lu\n", b->nvmet_tcp_cmd_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_submit_to_blk: %lu\n", b->nvmet_tcp_submit_to_blk/b->cnt/1000);
//     pr_info("nvmet_tcp_completion_queueing: %lu\n", b->nvmet_tcp_completion_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_response_processing: %lu\n", b->nvmet_tcp_response_processing/b->cnt/1000);
//     pr_info("nvme_tcp_waiting_for_resp: %lu\n", b->nvme_tcp_waiting_for_resp/b->cnt/1000);
//     pr_info("nvme_tcp_completion_queueing: %lu\n", b->nvme_tcp_completion_queueing/b->cnt/1000);
//     pr_info("blk_completion_queueing: %lu\n", b->blk_completion_queueing/b->cnt/1000);
//     pr_info("blk_e2e: %lu\n", b->blk_e2e/b->cnt/1000);
//     pr_info("nvme_tcp_e2e: %lu\n", b->nvme_tcp_e2e/b->cnt/1000);
//     pr_info("nvmet_tcp_e2e: %lu\n", b->nvmet_tcp_e2e/b->cnt/1000);
//     pr_info("networking: %lu\n", b->networking/b->cnt/1000);
// }

// struct write_breakdown_l {
//     u64 cnt;
//     // blk layer
//     u64 blk_submission_queueing;
//     // nvme-tcp layer
//     u64 nvme_tcp_cmd_submission_queueing;
//     u64 nvme_tcp_cmd_capsule_processing;
//
//     // nvmet-tcp layer
//     u64 nvmet_tcp_cmd_capsule_queueing;
//     u64 nvmet_tcp_cmd_processing;
//     u64 nvmet_tcp_r2t_submission_queueing;
//     u64 nvmet_tcp_r2t_resp_processing;
//     u64 nvmet_tcp_wait_for_data;
//     //nvme-tcp layer
//     u64 nvme_tcp_wait_for_r2t; // from sending the cmd to receiving the first byte of data
//     u64 nvme_tcp_r2t_resp_queueing;
//     u64 nvme_tcp_data_pdu_submission_queueing;
//     u64 nvme_tcp_data_pdu_processing;
//     // nvmet-tcp layer
//     u64 nvmet_tcp_write_cmd_queueing;
//     u64 nvmet_tcp_write_cmd_processing;
//     u64 nvmet_tcp_submit_to_blk;
//     u64 nvmet_tcp_completion_queueing;
//     u64 nvmet_tcp_response_processing;
//     // nvme-tcp layer
//     u64 nvme_tcp_wait_for_resp;
//     u64 nvme_tcp_resp_queueing;
//     // blk layer
//     u64 blk_completion_queueing;
//
//     // summary
//     u64 blk_e2e;
//     u64 networking;
// };
//
// static inline void init_write_breakdown_l(struct write_breakdown_l *b) {
//     memset(b, 0, sizeof(struct write_breakdown_l));
// }
//
// static inline void print_write_breakdown_l(struct write_breakdown_l *b) {
//     pr_info("latency breakdown: \n");
//     pr_info("blk_submission_queueing: %lu\n", b->blk_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_cmd_submission_queueing: %lu\n", b->nvme_tcp_cmd_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_cmd_capsule_processing: %lu\n", b->nvme_tcp_cmd_capsule_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_capsule_queueing: %lu\n", b->nvmet_tcp_cmd_capsule_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_cmd_processing: %lu\n", b->nvmet_tcp_cmd_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_r2t_submission_queueing: %lu\n", b->nvmet_tcp_r2t_submission_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_r2t_resp_processing: %lu\n", b->nvmet_tcp_r2t_resp_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_wait_for_data: %lu\n", b->nvmet_tcp_wait_for_data/b->cnt/1000);
//     pr_info("nvme_tcp_wait_for_r2t: %lu\n", b->nvme_tcp_wait_for_r2t/b->cnt/1000);
//     pr_info("nvme_tcp_r2t_resp_queueing: %lu\n", b->nvme_tcp_r2t_resp_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_data_pdu_submission_queueing: %lu\n", b->nvme_tcp_data_pdu_submission_queueing/b->cnt/1000);
//     pr_info("nvme_tcp_data_pdu_processing: %lu\n", b->nvme_tcp_data_pdu_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_write_cmd_queueing: %lu\n", b->nvmet_tcp_write_cmd_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_write_cmd_processing: %lu\n", b->nvmet_tcp_write_cmd_processing/b->cnt/1000);
//     pr_info("nvmet_tcp_submit_to_blk: %lu\n", b->nvmet_tcp_submit_to_blk/b->cnt/1000);
//     pr_info("nvmet_tcp_completion_queueing: %lu\n", b->nvmet_tcp_completion_queueing/b->cnt/1000);
//     pr_info("nvmet_tcp_response_processing: %lu\n", b->nvmet_tcp_response_processing/b->cnt/1000);
//     pr_info("nvme_tcp_wait_for_resp: %lu\n", b->nvme_tcp_wait_for_resp/b->cnt/1000);
//     pr_info("nvme_tcp_resp_queueing: %lu\n", b->nvme_tcp_resp_queueing/b->cnt/1000);
//     pr_info("blk_completion_queueing: %lu\n", b->blk_completion_queueing/b->cnt/1000);
//     pr_info("blk_e2e: %lu\n", b->blk_e2e/b->cnt/1000);
//     pr_info("networking: %lu\n", b->networking/b->cnt/1000);
// }

struct latency_breakdown {
};

void start_phase_1(struct per_core_statistics* stat,
                   struct ntprof_config* config);

void break_latency_write_s(struct profile_record* record,
                           struct write_breakdown_s* breakdown);

void break_latency_read(struct profile_record* record,
                        struct read_breakdown* breakdown);

void break_latency_write_l(struct profile_record* record,
                           struct write_breakdown_l* breakdown);

#endif // ANALYZER_H