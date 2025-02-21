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
#include <linux/delay.h>

#include "breakdown.h"

void print_current_event(int cid, int tag, int cmdid, char *event) {
    // pr_info("[event debug] cid %d: tag %d: %s, cmdid %d", cid, tag, event, cmdid);
}

void on_nvme_tcp_queue_rq(void *ignore, struct request *req, int qid, bool *to_trace, int len1, int len2,
                          long long unsigned int time) {
    // dump_stack();
    if (atomic_read(&trace_on) == 0 || qid == 0) {
        return;
    }

    int cid = smp_processor_id();
    print_current_event(cid, req->tag,-1, "on_nvme_tcp_queue_rq");

    if (unlikely(global_config.frequency == 0)) {
        pr_err("on_block_rq_complete: frequency is 0, no sampling");
        return;
    }

    if (++stat[cid].sampler >= global_config.frequency) {
        stat[cid].sampler = 0;
        if (match_config(req, &global_config)) {
            SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_rq");
            // pr_info("on_nvme_tcp_queue_rq, PID:%d, core_id:%d, queue_id:%d, tag:%d, time:%llu\n", current->pid, cid, qid, req->tag, time);
            if (unlikely(get_profile_record(&stat[cid], req))) {
                pr_err("Duplicated tag in incomplete list, cid=%d, tag=%d\n", cid, req->tag);
                SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_rq");
                return;
            }
            *to_trace = true;
            // pr_info("sample request %d", req->tag);

            //update_op_cnt(true);
            struct profile_record *record = kmalloc(sizeof(struct profile_record), GFP_KERNEL);
            if (!record) {
                pr_err("Failed to allocate memory for profile_record");
                //update_op_cnt(false);
                SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_rq");
                return;
            }
            long diff = ktime_get_real_ns() - ktime_get_ns();
            init_profile_record(record, blk_rq_bytes(req), rq_data_dir(req), req->rq_disk->disk_name, req->tag);
            record->metadata.req = req;
            // req->start_time_ns is initialized in blk-core.c, blk_rq_init
            // it was calling ktime_get_ns();
            append_event(record, req->start_time_ns + diff, BLK_SUBMIT);
            append_event(record, time + diff, NVME_TCP_QUEUE_RQ);
            append_record(&stat[cid], record);
            SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_rq");
        }
    }
}

void on_nvme_tcp_queue_request(void *ignore, struct request *req, int qid, int cmdid, bool is_initial,
                               long long unsigned int time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag,cmdid, "on_nvme_tcp_queue_request");
    unsigned long flags;
    //update_op_cnt(true);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_request");
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        append_event(rec, time, NVME_TCP_QUEUE_REQUEST);
        if (rec->metadata.cmdid == -1) {
            rec->metadata.cmdid = cmdid;
        }
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_queue_request");
    //update_op_cnt(false);
}

void on_nvme_tcp_try_send(void *ignore, struct request *req, int qid, long long unsigned int time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, -1,"on_nvme_tcp_try_send");
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        struct ts_entry *last_entry = list_last_entry(&rec->ts->list, struct ts_entry, list);
        if (last_entry->event != NVME_TCP_TRY_SEND_DATA) {
            append_event(rec, time, NVME_TCP_TRY_SEND);
        }
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send");
    //update_op_cnt(false);
}

void on_nvme_tcp_try_send_cmd_pdu(void *ignore, struct request *req, int qid, int len, int local_port,
                                  long long unsigned int time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, local_port,"cmdid is [bool inlinedata], on_nvme_tcp_try_send_cmd_pdu");
    // pr_info("len=%d", len);
    pr_debug("cid: %d, try send cmd pdu: req->tag=%d", cid, req->tag);
    //update_op_cnt(true);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_cmd_pdu");
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND_CMD_PDU);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_cmd_pdu");
    //update_op_cnt(false);
}

void on_nvme_tcp_try_send_data_pdu(void *ignore, struct request *req, int qid, int len, long long unsigned int time,
                                   void *pdu) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, len,"on_nvme_tcp_try_send_data_pdu");
    pr_debug("cid: %d, try send data pdu: req->tag=%d", cid, req->tag);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_data_pdu");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        append_event(rec, time, NVME_TCP_TRY_SEND_DATA_PDU);
        struct nvme_tcp_data_pdu *data_pdu = (struct nvme_tcp_data_pdu *) pdu;
        data_pdu->stat.tag = true;
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_data_pdu");
    //update_op_cnt(false);
}

void on_nvme_tcp_try_send_data(void *ignore, struct request *req, int qid, int len, long long unsigned int time,
                               void *nul) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, len,"on_nvme_tcp_try_send_data");
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_data");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        // get last event
        struct ts_entry *last_entry = list_last_entry(&rec->ts->list, struct ts_entry, list);
        if (last_entry->event != NVME_TCP_TRY_SEND) {
            append_event(rec, time, NVME_TCP_TRY_SEND_DATA);
        }
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_try_send_data");
    //update_op_cnt(false);
}

void on_nvme_tcp_done_send_req(void *ignore, struct request *req, int qid, long long unsigned int time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, -1,"on_nvme_tcp_done_send_req");
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_done_send_req");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        append_event(rec, time, NVME_TCP_DONE_SEND_REQ);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_done_send_req");
    //update_op_cnt(false);
}

void on_nvme_tcp_handle_c2h_data(void *ignore, struct request *rq, int qid, int data_remain, unsigned long long time,
                                 long long recv_time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, rq->tag, -1,"on_nvme_tcp_handle_c2h_data");
    pr_debug("cid: %d, handle c2h data: req->tag=%d", cid, rq->tag);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_handle_c2h_data");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], rq);
    // pr_info("on_nvme_tcp_handle_c2h_data is called: current incomplete list length on core %d is %d", cid,
    //         get_list_len(&stat[cid]));

    if (rec) {
        rec->metadata.contains_c2h = 1;
        // check the last timestamp
        struct ts_entry *last_entry = list_last_entry(&rec->ts->list, struct ts_entry, list);
        unsigned long long lasttime = last_entry->timestamp;
        if (recv_time < lasttime) {
            pr_info("on_nvme_tcp_handle_c2h_data is called: current incomplete list length on core %d is %d", cid,
                    get_list_len(&stat[cid]));
            pr_err("on_nvme_tcp_handle_c2h_data: to append time %llu is less than last time %llu\n", recv_time,
                   lasttime);
            print_profile_record(rec);
        }

        append_event(rec, recv_time, NVME_TCP_RECV_SKB);
        append_event(rec, time, NVME_TCP_HANDLE_C2H_DATA);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_handle_c2h_data");
    //update_op_cnt(false);
}

void on_nvme_tcp_recv_data(void *ignore, struct request *rq, int qid, int cp_len, unsigned long long time,
                           long long recv_time) {
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, rq->tag, -1, "on_nvme_tcp_recv_data");
    //update_op_cnt(true);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_recv_data");
    struct profile_record *rec = get_profile_record(&stat[cid], rq);
    if (rec) {
        append_event(rec, time, NVME_TCP_RECV_DATA);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_recv_data");
    //update_op_cnt(false);
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
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, -1,"on_nvme_tcp_handle_r2t");
    pr_debug("cid: %d, handle r2t: req->tag=%d", cid, req->tag);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_handle_r2t");
    //update_op_cnt(true);
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    if (rec) {
        if (((struct nvme_tcp_r2t_pdu *) pdu)->stat.id != (unsigned long long) rec->metadata.cmdid) {
            pr_warn("stat.id=%llu, metadata.cmdid=%d\n", ((struct nvme_tcp_r2t_pdu *) pdu)->stat.id,
                    rec->metadata.cmdid);
        } else {
            rec->metadata.contains_r2t = 1;
            //
            // struct ts_entry *last_entry = list_last_entry(&rec->ts->list, struct ts_entry, list);
            // unsigned long long lasttime = last_entry->timestamp;
            // if (recv_time < lasttime) {
            //     pr_info("on_nvme_tcp_handle_r2t is called: current incomplete list length on core %d is %d", cid,
            //             get_list_len(&stat[cid]));
            //     pr_err("on_nvme_tcp_handle_r2t: to append time %llu is less than last time %llu\n", recv_time,
            //            lasttime);
            //     print_profile_record(rec);
            // }
            cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_r2t_pdu *) pdu)->stat);
        }

        append_event(rec, recv_time, NVME_TCP_RECV_SKB);
        append_event(rec, time, NVME_TCP_HANDLE_R2T);
    } else {
        pr_err(
            "!!! on_nvme_tcp_handle_r2t: rec [tag=%d] is not found or it is NULL, stat->cmdid=%llu, the incomplete queue is: ",
            req->tag, ((struct nvme_tcp_r2t_pdu *) pdu)->stat.id);
        // print_incomplete_queue(&stat[cid]);
        // msleep(20000);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_handle_r2t");
    //update_op_cnt(false);
}

void on_nvme_tcp_process_nvme_cqe(void *ignore, struct request *req, int qid, unsigned long long time,
                                  long long recv_time, void *pdu) {
    // dump_stack();
    if (atomic_read(&trace_on) == 0 || qid == 0) return;
    int cid = smp_processor_id();
    print_current_event(cid, req->tag, -1,"on_nvme_tcp_process_nvme_cqe");
    pr_debug("cid: %d, process nvme cqe: req->tag=%d", cid, req->tag);
    //update_op_cnt(true);
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_process_nvme_cqe");
    struct profile_record *rec = get_profile_record(&stat[cid], req);
    // pr_info("on_nvme_tcp_process_nvme_cqe is called: current incomplete list length on core %d is %d", cid,
    //         get_list_len(&stat[cid]));
    if (rec) {
        if (((struct nvme_tcp_rsp_pdu *) pdu)->stat.id != (unsigned long long) rec->metadata.cmdid) {
            pr_warn("stat.id=%llu, metadata.cmdid=%d\n", ((struct nvme_tcp_rsp_pdu *) pdu)->stat.id,
                    rec->metadata.cmdid);
        } else {
            // if the new timestamp is less than the last timestamp
            // print an error message
            struct ts_entry *last_entry = list_last_entry(&rec->ts->list, struct ts_entry, list);
            unsigned long long lasttime = last_entry->timestamp;
            // if (recv_time < lasttime) {
            //     pr_err("to append time %llu is less than last time %llu\n", time, lasttime);
            //     print_profile_record(rec);
            // }
            cpy_ntprof_stat_to_record(rec, &((struct nvme_tcp_rsp_pdu *) pdu)->stat);
            append_event(rec, recv_time, NVME_TCP_RECV_SKB);
            append_event(rec, time, NVME_TCP_PROCESS_NVME_CQE);
        }
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[cid].lock, "on_nvme_tcp_process_nvme_cqe");
    //update_op_cnt(false);
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
