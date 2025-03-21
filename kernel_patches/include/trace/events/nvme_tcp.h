#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvme_tcp

#if !defined(_TRACE_NVME_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVME_TCP_H

#include <linux/tracepoint.h>
#include <linux/nvme.h>
#include <linux/trace_seq.h>
#include <linux/net.h>

#ifndef __nvme_tcp_print_disk_name
#define __nvme_tcp_print_disk_name(name) nvme_tcp_trace_disk_name(p, name)
#endif


#ifndef _TRACE_NVME_TCP_TRACE_DISK_NAME_DEFINED
#define _TRACE_NVME_TCP_TRACE_DISK_NAME_DEFINED
static inline const char *nvme_tcp_trace_disk_name(struct trace_seq *p, char *name)
{
	const char *ret = trace_seq_buffer_ptr(p);

	if (*name)
		trace_seq_printf(p, "disk=%s, ", name);
	trace_seq_putc(p, 0);

	return ret;
}
#endif

TRACE_EVENT(nvme_tcp_queue_rq,
    TP_PROTO(struct request *req, void *pdu, int qid, struct llist_head *req_list, struct list_head *send_list, struct mutex *send_mutex),
    TP_ARGS(req, pdu, qid, req_list, send_list, send_mutex),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(u64, slba)
        __field(u64, pos)
        __field(u32, length)
        __field(int, r_tag)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(req)->ctrl->instance;
        __entry->qid = nvme_req_qid(req);
        __entry->cid = nvme_req(req)->cmd->common.command_id;
        __entry->slba = le64_to_cpu(nvme_req(req)->cmd->rw.slba);
        __entry->pos = blk_rq_pos(req);
        __entry->length = blk_rq_bytes(req);
        __assign_disk_name(__entry->disk, req->rq_disk);
        __entry->r_tag = req->tag;
        __entry->is_write = (rq_data_dir(req) == WRITE);
    ),
    /** print all fields */
    /** we can use __nvme_tcp_print_disk_name(__entry->disk) to print disk name
     * but it requires export some extra symbol, so we are not using it
     */
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, pos=%llu, length=%u, is_write=%d",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->r_tag, __entry->cid, __entry->slba, __entry->pos, __entry->length, __entry->is_write)
);


/**
 * tag = 1 if the request is the initial request, 0 if it is a subsequent data pdu
*/
TRACE_EVENT(nvme_tcp_queue_request,
    TP_PROTO(struct request *req, struct nvme_command* cmd, int qid),
    TP_ARGS(req, cmd, qid),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(req)->ctrl->instance;
        __entry->qid = nvme_req_qid(req);
        __entry->cid = nvme_req(req)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, req->rq_disk);
        __entry->req_tag = req->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);


TRACE_EVENT(nvme_tcp_try_send_cmd_pdu,
    TP_PROTO(struct request *rq, struct socket *sock, int qid, int len),
    TP_ARGS(rq, sock, qid, len),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);


TRACE_EVENT(nvme_tcp_try_send_data_pdu,
    TP_PROTO(struct request *rq, void *pdu, int qid),
    TP_ARGS(rq, pdu, qid),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);

DEFINE_EVENT(nvme_tcp_try_send_data_pdu, nvme_tcp_try_send_data,
    TP_PROTO(struct request *rq, void *pdu, int qid),
    TP_ARGS(rq, pdu, qid)
);

TRACE_EVENT(nvme_tcp_done_send_req,
    TP_PROTO(struct request *rq, int qid),
    TP_ARGS(rq, qid),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);

// TRACE_EVENT(nvme_tcp_recv_skb,
//     TP_PROTO(struct request *rq, struct sk_buff *skb, int qid, int offset, size_t len),
//     TP_ARGS(rq, skb, qid, offset, len),
//     TP_STRUCT__entry(
//         __array(char, disk, DISK_NAME_LEN)
//         __field(int, ctrl_id)
//         __field(int, qid)
//         __field(int, cid)
//         __field(int, req_tag)
//     ),
//     TP_fast_assign(
//         __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
//         __entry->qid = nvme_req_qid(rq);
//         __entry->cid = nvme_req(rq)->cmd->common.command_id;
//         __assign_disk_name(__entry->disk, rq->rq_disk);
//         __entry->req_tag = rq->tag;
//     ),
//     TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
//         __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
//         __entry->qid, __entry->req_tag, __entry->cid)
// );    

// TRACE_EVENT(nvme_tcp_recv_pdu,
//     TP_PROTO(int consumed, unsigned char pdu_type, int qid, unsigned long long time),
//     TP_ARGS(consumed, pdu_type, qid, time),
//     TP_STRUCT__entry(
//         __field(int, consumed)
//         __field(unsigned char, pdu_type)
//         __field(int, qid)
//     ),
//     TP_fast_assign(
//         __entry->consumed = consumed;
//         __entry->pdu_type = pdu_type;
//         __entry->qid = qid;
//         // if(__entry->qid == 0) return;
//     ),
//     TP_printk("consumed=%d, pdu_type=%u, qid=%d",
//         __entry->consumed, __entry->pdu_type, __entry->qid)
// );

TRACE_EVENT(nvme_tcp_handle_c2h_data, 
    TP_PROTO(struct request* rq, int qid, int data_remain, u64 recv_time),
    TP_ARGS(rq, qid, data_remain, recv_time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);

TRACE_EVENT(nvme_tcp_recv_data,
    TP_PROTO(struct request* rq, int qid, int len),
    TP_ARGS(rq, qid, len),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);

TRACE_EVENT(nvme_tcp_process_nvme_cqe,
    TP_PROTO(struct request *rq, void* pdu, int qid, u64 recv_time),
    TP_ARGS(rq, pdu, qid, recv_time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(int, req_tag)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->req_tag = rq->tag;
    ),
    /** print all fields */
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u",
        __entry->ctrl_id, __nvme_tcp_print_disk_name(__entry->disk),
        __entry->qid, __entry->req_tag, __entry->cid)
);

DEFINE_EVENT(nvme_tcp_process_nvme_cqe, nvme_tcp_handle_r2t,
    TP_PROTO(struct request *rq, void* pdu, int qid, u64 recv_time),
    TP_ARGS(rq, pdu, qid, recv_time)
);

DEFINE_EVENT(nvme_tcp_done_send_req, nvme_tcp_try_send,
    TP_PROTO(struct request *rq, int qid),
    TP_ARGS(rq, qid)
);

#endif /* _TRACE_NVME_TCP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nvme_tcp
#include <trace/define_trace.h>
