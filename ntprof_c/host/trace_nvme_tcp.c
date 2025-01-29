#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/nvme.h>
#include <linux/proc_fs.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>
#include "../include/statistics.h"

void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int qid, bool *to_trace, int len1, int len2,
                          long long unsigned int time) {
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int qid, int cmdid, bool is_initial,
                               long long unsigned int time) {
}

void on_nvme_tcp_try_send(void *ignore, struct request *req, int qid, long long unsigned int time) {
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int qid, int len, int local_port,
                                  long long unsigned int time) {
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int qid, int len, long long unsigned int time) {
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int qid, int len, long long unsigned int time) {
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req, int qid, long long unsigned int time) {
}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq, int qid, int data_remain, unsigned long long time,
                                 long long recv_time) {
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int qid, int cp_len, unsigned long long time,
                           long long recv_time) {
}

void on_nvme_tcp_handle_r2t(void *ignore, struct request *req, int qid, unsigned long long time, long long recv_time) {
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req, int qid, unsigned long long time,
                                  long long recv_time) {
}

int register_nvme_tcp_tracepoints(void) {
    int ret;

    // Register the tracepoints
    if ((ret = register_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL)))
        goto failed;
    if ((ret = register_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request, NULL)))
        goto unregister_queue_rq;
    if ((ret = register_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL)))
        goto unregister_queue_request;
    if ((ret = register_trace_nvme_tcp_try_send_cmd_pdu(on_nvme_tcp_try_send_cmd_pdu, NULL)))
        goto unregister_try_send;
    if ((ret = register_trace_nvme_tcp_try_send_data_pdu(on_nvme_tcp_try_send_data_pdu, NULL)))
        goto unregister_try_send_cmd_pdu;
    if ((ret = register_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data, NULL)))
        goto unregister_try_send_data_pdu;
    if ((ret = register_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req, NULL)))
        goto unregister_try_send_data;
    if ((ret = register_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL)))
        goto unregister_done_send_req;
    if ((ret = register_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL)))
        goto unregister_handle_c2h_data;
    if ((ret = register_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL)))
        goto unregister_recv_data;
    if ((ret = register_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe, NULL)))
        goto unregister_handle_r2t;

    return 0;

    // rollback
unregister_handle_r2t:
    unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
unregister_recv_data:
    unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
unregister_handle_c2h_data:
    unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
unregister_done_send_req:
    unregister_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req, NULL);
unregister_try_send_data:
    unregister_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data, NULL);
unregister_try_send_data_pdu:
    unregister_trace_nvme_tcp_try_send_data_pdu(on_nvme_tcp_try_send_data_pdu, NULL);
unregister_try_send_cmd_pdu:
    unregister_trace_nvme_tcp_try_send_cmd_pdu(on_nvme_tcp_try_send_cmd_pdu, NULL);
unregister_try_send:
    unregister_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL);
unregister_queue_request:
    unregister_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request, NULL);
unregister_queue_rq:
    unregister_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
failed:
    return ret;
}

void unregister_nvme_tcp_tracepoints(void) {
    unregister_trace_nvme_tcp_queue_rq(on_nvme_tcp_queue_rq, NULL);
    unregister_trace_nvme_tcp_queue_request(on_nvme_tcp_queue_request, NULL);
    unregister_trace_nvme_tcp_try_send(on_nvme_tcp_try_send, NULL);
    unregister_trace_nvme_tcp_try_send_cmd_pdu(on_nvme_tcp_try_send_cmd_pdu, NULL);
    unregister_trace_nvme_tcp_try_send_data_pdu(on_nvme_tcp_try_send_data_pdu, NULL);
    unregister_trace_nvme_tcp_try_send_data(on_nvme_tcp_try_send_data, NULL);
    unregister_trace_nvme_tcp_done_send_req(on_nvme_tcp_done_send_req, NULL);
    unregister_trace_nvme_tcp_handle_c2h_data(on_nvme_tcp_handle_c2h_data, NULL);
    unregister_trace_nvme_tcp_recv_data(on_nvme_tcp_recv_data, NULL);
    unregister_trace_nvme_tcp_handle_r2t(on_nvme_tcp_handle_r2t, NULL);
    unregister_trace_nvme_tcp_process_nvme_cqe(on_nvme_tcp_process_nvme_cqe, NULL);
}