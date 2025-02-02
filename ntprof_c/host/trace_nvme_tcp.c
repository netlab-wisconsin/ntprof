#include <linux/blk-mq.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/nvme-tcp.h>
#include <linux/nvme.h>
#include <linux/proc_fs.h>
#include <linux/tracepoint.h>
#include <trace/events/nvme_tcp.h>
#include <linux/timekeeping.h>
#include "../include/statistics.h"
#include "host.h"
#include "host_logging.h"

void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int qid, bool *to_trace, int len1, int len2,
                          long long unsigned int time) {
    if (qid == 0) {
        return;
    }
    int cid = smp_processor_id();

    if (unlikely(global_config.frequency == 0)) {
        pr_err("on_block_rq_complete: frequency is 0, no sampling");
        return;
    }

    if (++stat[cid].sampler >= global_config.frequency) {
        stat[cid].sampler = 0;
        if (match_config(req, &global_config)) {
            if (unlikely(get_profile_record(&stat[cid], req->tag))) {
                pr_err("Duplicated tag in incomplete list, cid=%d, tag=%d\n", cid, req->tag);
                return;
            }
            *to_trace = true;
            // pr_info("sample request %d", req->tag);
            struct profile_record *record = kmalloc(sizeof(struct profile_record), GFP_KERNEL);
            if (!record) {
                pr_err("Failed to allocate memory for profile_record");
                return;
            }
            long diff = ktime_get_real_ns() - ktime_get_ns();
            init_profile_record(record, blk_rq_bytes(req), rq_data_dir(req), req->rq_disk->disk_name, req->tag);
            // req->start_time_ns is initialized in blk-core.c, blk_rq_init
            // it was calling ktime_get_ns();
            append_event(record, req->start_time_ns + diff, BLK_SUBMIT);
            append_event(record, time + diff, NVME_TCP_QUEUE_RQ);
            append_record(&stat[cid], record);
        }
    }
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int qid, int cmdid, bool is_initial,
                               long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_QUEUE_REQUEST);
    }
}

void on_nvme_tcp_try_send(void *ignore, struct request *req, int qid, long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND);
    }
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int qid, int len, int local_port,
                                  long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND_CMD_PDU);
    }
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int qid, int len, long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND_DATA_PDU);
    }
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int qid, int len, long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND_DATA);
    }
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req, int qid, long long unsigned int time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_DONE_SEND_REQ);
    }
}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq, int qid, int data_remain, unsigned long long time,
                                 long long recv_time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], rq->tag);
    if (rec) {
        rec->metadata.contains_c2h=1;
        append_event(rec, time, NVME_TCP_HANDLE_C2H_DATA);
    }
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int qid, int cp_len, unsigned long long time,
                           long long recv_time) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], rq->tag);
    if (rec) {
        append_event(rec, time, NVME_TCP_RECV_DATA);
    }
}

void cpy_ntprof_stat_to_record(struct profile_record *record, struct ntprof_stat *pdu_stat) {
    if (unlikely(pdu_stat == NULL)) {
        pr_err("try to copy time series from pdu_stat but it is NULL");
        return;
    }
    int i;
    for (i = 0; i < pdu_stat->cnt; i++) {
        append_event(record, pdu_stat->ts[i], pdu_stat->event[i]);
    }
}


void on_nvme_tcp_handle_r2t(void *ignore, struct request *req, int qid, unsigned long long time, long long recv_time,
                            void *pdu) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        rec->metadata.contains_r2t = 1;
        cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_r2t_pdu *) pdu)->stat);
        append_event(rec, time, NVME_TCP_HANDLE_R2T);
    }
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req, int qid, unsigned long long time,
                                  long long recv_time, void *pdu) {
    int cid = smp_processor_id();
    struct profile_record *rec = get_profile_record(&stat[cid], req->tag);
    if (rec) {
        cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_rsp_pdu *) pdu)->stat);
        append_event(rec, time, NVME_TCP_PROCESS_NVME_CQE);
    }
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
