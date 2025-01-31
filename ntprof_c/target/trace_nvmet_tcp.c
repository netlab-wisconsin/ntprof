#include "trace_nvmet_tcp.h"
#include <linux/tracepoint.h>
#include <trace/events/nvmet_tcp.h>

void on_try_recv_pdu(void *ignore, u8 pdu_type, u8 hdr_len, int queue_left, int qid, int remote_port,
                     unsigned long long time) {
}

void on_done_recv_pdu(void *ignore, u16 cmd_id, int qid, u8 opcode, int size, unsigned long long time,
                      long long recv_time) {
}

void on_exec_read_req(void *ignore, u16 cmd_id, int qid, u8 opcode, int size, unsigned long long time) {
}

void on_exec_write_req(void *ignore, u16 cmd_id, int qid, u8 opcode, int size, unsigned long long time) {
}

// TODO: use opcode instead
void on_queue_response(void *ignore, u16 cmd_id, int qid, bool is_write, unsigned long long time) {
}

void on_setup_c2h_data_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
}

void on_setup_r2t_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
}

void on_setup_response_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
}

void on_try_send_data_pdu(void *ignore, u16 cmd_id, int qid, int cp_len, int left, unsigned long long time) {
}

void on_try_send_r2t(void *ignore, u16 cmd_id, int qid, int cp_len, int left, unsigned long long time) {
}

void on_try_send_response(void *ignore, u16 cmd_id, int qid, int cp_len, int left, int is_write,
                          unsigned long long time) {
}

void on_try_send_data(void *ignore, u16 cmd_id, int qid, int cp_len, unsigned long long time) {
}

void on_try_recv_data(void *ignore, u16 cmd_id, int qid, int cp_len, unsigned long long time, long long recv_time) {
}

void on_handle_h2c_data_pdu(void *ignore, u16 cmd_id, int qid, int datalen, unsigned long long time,
                            long long recv_time) {
}

void on_io_work(void *ignore, int qid, long long recv, long long send) {
}

int register_nvmet_tcp_tracepoints(void) {
    int ret;

    if ((ret = register_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL)))
        goto failed;
    if ((ret = register_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL)))
        goto unregister_try_recv_pdu;
    if ((ret = register_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL)))
        goto unregister_done_recv_pdu;
    if ((ret = register_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL)))
        goto unregister_exec_read_req;
    if ((ret = register_trace_nvmet_tcp_queue_response(on_queue_response, NULL)))
        goto unregister_exec_write_req;
    if ((ret = register_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL)))
        goto unregister_queue_response;
    if ((ret = register_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL)))
        goto unregister_setup_c2h_data_pdu;
    if ((ret = register_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL)))
        goto unregister_setup_r2t_pdu;
    if ((ret = register_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL)))
        goto unregister_setup_response_pdu;
    if ((ret = register_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL)))
        goto unregister_try_send_data_pdu;
    if ((ret = register_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL)))
        goto unregister_try_send_r2t;
    if ((ret = register_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL)))
        goto unregister_try_send_response;
    if ((ret = register_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL)))
        goto unregister_try_send_data;
    if ((ret = register_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL)))
        goto unregister_handle_h2c_data_pdu;
    if ((ret = register_trace_nvmet_tcp_io_work(on_io_work, NULL)))
        goto unregister_try_recv_data;

    return 0;

    // rollback
unregister_try_recv_data:
    unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
unregister_handle_h2c_data_pdu:
    unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
unregister_try_send_data:
    unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
unregister_try_send_response:
    unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
unregister_try_send_r2t:
    unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
unregister_try_send_data_pdu:
    unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
unregister_setup_response_pdu:
    unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
unregister_setup_r2t_pdu:
    unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
unregister_setup_c2h_data_pdu:
    unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
unregister_queue_response:
    unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
unregister_exec_write_req:
    unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
unregister_exec_read_req:
    unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
unregister_done_recv_pdu:
    unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
unregister_try_recv_pdu:
    unregister_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL);
failed:
    pr_info("failed to register nvmet tracepoints\n");
    return ret;
}


void unregister_nvmet_tcp_tracepoints(void) {
    unregister_trace_nvmet_tcp_try_recv_pdu(on_try_recv_pdu, NULL);
    unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
    unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
    unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
    unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
    unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
    unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
    unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
    unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
    unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
    unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
    unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
    unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
    unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
    unregister_trace_nvmet_tcp_io_work(on_io_work, NULL);
}
