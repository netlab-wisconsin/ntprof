#include "trace_nvmet_tcp.h"

#include <linux/slab.h>
#include <linux/tracepoint.h>
#include <trace/events/nvmet_tcp.h>
#include <linux/nvme-tcp.h>

#include "target.h"
#include "../include/statistics.h"
#include "target_logging.h"


void on_try_recv_pdu(void *ignore, u8 pdu_type, u8 hdr_len, int queue_left, int qid, int remote_port,
                     unsigned long long time) {
}

void on_done_recv_pdu(void *ignore, u16 cmdid, int qid, u8 opcode, int size, unsigned long long time,
                      long long recv_time) {
    pr_info("on_done_recv_pdu is called!");
    if (unlikely(get_profile_record(&stat[qid], cmdid))) {
        pr_err("Duplicated cmdid %d in the record list %d\n", cmdid, qid);
        return;
    }
    int is_write;
    if (opcode == nvme_cmd_read) {
        is_write = 0;
    } else if (opcode == nvme_cmd_write) {
        is_write = 1;
    } else {
        pr_err("Unknown opcode in on_done_recv_pdu: %d\n", opcode);
        return;
    }
    //TODO: manually release the memory
    struct profile_record *record = kmalloc(sizeof(struct profile_record), GFP_KERNEL);
    if (!record) {
        pr_err("Failed to allocate memory for profile_record");
        return;
    }

    init_profile_record(record, size, is_write, "", cmdid);
    append_event(record, recv_time, NVMET_TCP_TRY_RECV_PDU);
    append_event(record, time, NVMET_TCP_DONE_RECV_PDU);
    append_record(&stat[qid], record);
}

void on_exec_read_req(void *ignore, u16 cmdid, int qid, u8 opcode, int size, unsigned long long time) {
    if (opcode != nvme_cmd_read) {
        pr_warn("Invalid opcode in on_exec_read_req: %d\n", opcode);
        return;
    }
    struct profile_record *record = get_profile_record(&stat[qid], cmdid);
    if (record) {
        append_event(record, time, NVMET_TCP_EXEC_READ_REQ);
    }
}

void on_exec_write_req(void *ignore, u16 cmd_id, int qid, u8 opcode, int size, unsigned long long time) {
    if (opcode != nvme_cmd_write) {
        pr_err("Invalid opcode in on_exec_write_req: %d\n", opcode);
        return;
    }
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_EXEC_WRITE_REQ);
    }
}

// TODO: use opcode instead
void on_queue_response(void *ignore, u16 cmd_id, int qid, bool is_write, unsigned long long time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_QUEUE_RESPONSE);
    }
}

void on_setup_c2h_data_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_SETUP_C2H_DATA_PDU);
    }
}

void on_setup_r2t_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_SETUP_R2T_PDU);
    }
}

void on_setup_response_pdu(void *ignore, u16 cmd_id, int qid, unsigned long long time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_SETUP_RESPONSE_PDU);
    }
}

void cpy_stat(struct profile_record *record, struct ntprof_stat *s) {
    s->cnt = 0;
    pr_info("copy stat to record: %d\n", record->metadata.req_tag);
    s->id = record->metadata.req_tag;
    if (s->id == 0) {
        pr_warn("making s->id 0!!!!!!!!!");
    }
    // taverse record->ts
    struct ts_entry *entry;
    list_for_each_entry(entry, &record->ts->list, list) {
        if (s->cnt >= 16) {
            pr_warn("Cnt exceeds 16, skip the rest\n");
            break;
        }
        s->ts[s->cnt] = entry->timestamp;
        s->event[s->cnt] = entry->event;
        s->cnt++;
    }
}

void on_try_send_data_pdu(void *ignore, u16 cmd_id, int qid, int size, unsigned long long time, void *pdu) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        // pr_info("on_try_send_data_pdu is called -> ");
        // print_profile_record(record);
        // (struct nvme_tcp_data_pdu *)pdu;
        append_event(record, time, NVMET_TCP_TRY_SEND_DATA_PDU);
        // cpy_stat(record, &((struct nvme_tcp_data_pdu *)pdu)->stat);
        // list_del_init(&record->list);
        // free_profile_record(record);
    }
}

void on_try_send_r2t(void *ignore, u16 cmd_id, int qid, int size, unsigned long long time, void *pdu) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        pr_info("on_try_send_r2t is called -> ");
        print_profile_record(record);
        // (struct nvme_tcp_r2t_pdu *)pdu;
        append_event(record, time, NVMET_TCP_TRY_SEND_R2T);
        cpy_stat(record, &((struct nvme_tcp_data_pdu *)pdu)->stat);
        list_del_init(&record->list);
        free_profile_record(record);
    }
}

void on_try_send_response(void *ignore, u16 cmd_id, int qid, int size, int is_write, unsigned long long time,
                          void *pdu) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        pr_info("on_try_send_response is called -> ");
        print_profile_record(record);
        // (struct nvme_tcp_rsp_pdu *)pdu;
        append_event(record, time, NVMET_TCP_TRY_SEND_RESPONSE);
        cpy_stat(record, &((struct nvme_tcp_data_pdu *)pdu)->stat);
        list_del_init(&record->list);
        free_profile_record(record);
    }
}

void on_try_send_data(void *ignore, u16 cmd_id, int qid, int cp_len, unsigned long long time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_TRY_SEND_DATA);
    }
}

void on_try_recv_data(void *ignore, u16 cmd_id, int qid, int cp_len, unsigned long long time, long long recv_time) {
    struct profile_record *record = get_profile_record(&stat[qid], cmd_id);
    if (record) {
        append_event(record, time, NVMET_TCP_TRY_RECV_DATA);
    }
}

void on_handle_h2c_data_pdu(void *ignore, u16 cmd_id, int qid, int datalen, unsigned long long time,
                            long long recv_time) {

    struct profile_record *record = kmalloc(sizeof(struct profile_record), GFP_KERNEL);
    if (!record) {
        pr_err("Failed to allocate memory for profile_record");
        return;
    }
    init_profile_record(record, datalen, true, "", cmd_id);
    append_event(record, recv_time, NVMET_TCP_TRY_RECV_PDU);
    append_event(record, time, NVMET_TCP_HANDLE_H2C_DATA_PDU);
    append_record(&stat[qid], record);
    
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
