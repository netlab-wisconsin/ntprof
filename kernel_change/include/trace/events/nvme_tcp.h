#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvme_tcp

#if !defined(_TRACE_NVME_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVME_TCP_H

#include <linux/tracepoint.h>
#include <linux/nvme.h>

TRACE_EVENT(nvme_tcp_queue_rq,
    TP_PROTO(struct request *req, int req_len, int send_len, unsigned long long time),
    TP_ARGS(req, req_len, send_len, time),
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
        __field(int, req_len)
        __field(int, send_len)
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
        __entry->req_len = req_len;
        __entry->send_len = send_len;
    ),
    /** print all fields */
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, pos=%llu, length=%u, is_write=%d, req_len=%d, send_len=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->r_tag, __entry->cid, __entry->slba, __entry->pos, __entry->length, __entry->is_write,
        __entry->req_len, __entry->send_len)
);


/**
 * tag = 1 if the request is the initial request, 0 if it is a subsequent data pdu
*/
TRACE_EVENT(nvme_tcp_queue_request,
    TP_PROTO(struct request *req, bool tag,  unsigned long long time),
    TP_ARGS(req, tag, time),
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
        __field(bool, tag)
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
        __entry->tag = tag;
    ),
    /** print all fields */
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, pos=%llu, length=%u, is_write=%d, is_init=%s",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->r_tag, __entry->cid, __entry->slba, __entry->pos, __entry->length, __entry->is_write, __entry->tag?"true":"false")
);


TRACE_EVENT(nvme_tcp_try_send_cmd_pdu,
    TP_PROTO(struct request *rq, int len, unsigned long long time),
    TP_ARGS(rq, len, time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(u64, slba)
        __field(u32, length)
        __field(int, rtag)
        __field(int, len)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __entry->slba = le64_to_cpu(nvme_req(rq)->cmd->rw.slba);
        __entry->length = blk_rq_bytes(rq);
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->rtag = rq->tag;
        __entry->len = len;
        __entry->is_write = (rq_data_dir(rq) == WRITE);
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, request_length=%u, send_bytes=%d, is_write=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->rtag, __entry->cid, __entry->slba, __entry->length, __entry->len, __entry->is_write)
);


TRACE_EVENT(nvme_tcp_try_send_data_pdu,
    TP_PROTO(struct request *rq, int len, unsigned long long time),
    TP_ARGS(rq, len, time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(u64, slba)
        __field(u32, length)
        __field(int, rtag)
        __field(int, len)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __entry->slba = le64_to_cpu(nvme_req(rq)->cmd->rw.slba);
        __entry->length = blk_rq_bytes(rq);
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->rtag = rq->tag;
        __entry->len = len;
        __entry->is_write = (rq_data_dir(rq) == WRITE);
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, req_len=%u, send_len=%d, is_write=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->rtag, __entry->cid, __entry->slba, __entry->length, __entry->len, __entry->is_write)
);

DEFINE_EVENT(nvme_tcp_try_send_data_pdu, nvme_tcp_try_send_data,
    TP_PROTO(struct request *rq, int len, unsigned long long time),
    TP_ARGS(rq, len, time)
);

TRACE_EVENT(nvme_tcp_done_send_req,
    TP_PROTO(struct request *rq, unsigned long long time),
    TP_ARGS(rq, time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, cid)
        __field(u64, slba)
        __field(u32, length)
        __field(int, rtag)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
        __entry->slba = le64_to_cpu(nvme_req(rq)->cmd->rw.slba);
        __entry->length = blk_rq_bytes(rq);
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->rtag = rq->tag;
        __entry->is_write = (rq_data_dir(rq) == WRITE);
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, req_len=%u, is_write=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->rtag, __entry->cid, __entry->slba, __entry->length, __entry->is_write)
);

TRACE_EVENT(nvme_tcp_try_recv,
    TP_PROTO(int offset, size_t len, int recv_stat, int qid, unsigned long long time),
    TP_ARGS(offset, len, recv_stat, qid, time),
    TP_STRUCT__entry(
        __field(int, offset)
        __field(size_t, len)
        __field(int, recv_stat)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->offset = offset;
        __entry->len = len;
        __entry->recv_stat = recv_stat;
        __entry->qid = qid;
        // if(__entry->qid == 0) return;
    ),
    TP_printk("offset=%d, len=%zu, recv_stat=%d, qid=%d",
        __entry->offset, __entry->len, __entry->recv_stat, __entry->qid)
);

TRACE_EVENT(nvme_tcp_recv_pdu,
    TP_PROTO(int consumed, unsigned char pdu_type, int qid, unsigned long long time),
    TP_ARGS(consumed, pdu_type, qid, time),
    TP_STRUCT__entry(
        __field(int, consumed)
        __field(unsigned char, pdu_type)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->consumed = consumed;
        __entry->pdu_type = pdu_type;
        __entry->qid = qid;
        // if(__entry->qid == 0) return;
    ),
    TP_printk("consumed=%d, pdu_type=%u, qid=%d",
        __entry->consumed, __entry->pdu_type, __entry->qid)
);

TRACE_EVENT(nvme_tcp_handle_c2h_data, 
    TP_PROTO(struct request* rq, int data_remain, int qid, unsigned long long time, unsigned long long skb_time),
    TP_ARGS(rq, data_remain, qid, time, skb_time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, rtag)
        __field(int, data_remain)
        __field(bool, is_write)
        __field(int, cid)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->rtag = rq->tag;
        __entry->data_remain = data_remain;
        __entry->is_write = (rq_data_dir(rq) == WRITE);
        __entry->cid = nvme_req(rq)->cmd->common.command_id;
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, data_remain=%d, is_write=%d, cmdid=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->rtag, __entry->data_remain, __entry->is_write, __entry->cid)
);

TRACE_EVENT(nvme_tcp_recv_data,
    TP_PROTO(struct request* rq, int cp_len, int qid, unsigned long long time, unsigned long long skb_time),
    TP_ARGS(rq, cp_len, qid, time, skb_time),
    TP_STRUCT__entry(
        __array(char, disk, DISK_NAME_LEN)
        __field(int, ctrl_id)
        __field(int, qid)
        __field(int, rtag)
        __field(int, cp_len)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->ctrl_id = nvme_req(rq)->ctrl->instance;
        __entry->qid = nvme_req_qid(rq);
        __assign_disk_name(__entry->disk, rq->rq_disk);
        __entry->rtag = rq->tag;
        __entry->cp_len = cp_len;
        __entry->is_write = (rq_data_dir(rq) == WRITE);
    ),
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cp_len=%d, is_write=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->rtag, __entry->cp_len, __entry->is_write)

);




TRACE_EVENT(nvme_tcp_process_nvme_cqe,
    TP_PROTO(struct request *req, unsigned long long time, unsigned long long skb_time),
    TP_ARGS(req, time, skb_time),
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
    TP_printk("nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, pos=%llu, length=%u, is_write=%d",
        __entry->ctrl_id, __print_disk_name(__entry->disk),
        __entry->qid, __entry->r_tag, __entry->cid, __entry->slba, __entry->pos, __entry->length, __entry->is_write)
);


DEFINE_EVENT(nvme_tcp_process_nvme_cqe, nvme_tcp_handle_r2t,
    TP_PROTO(struct request *req, unsigned long long time, unsigned long long skb_time),
    TP_ARGS(req, time, skb_time)
);

DEFINE_EVENT(nvme_tcp_done_send_req, nvme_tcp_try_send,
    TP_PROTO(struct request *req, unsigned long long time),
    TP_ARGS(req, time)
);

#endif /* _TRACE_NVME_TCP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nvme_tcp
#include <trace/define_trace.h>
