#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvmet_tcp

#if !defined(_TRACE_NVMET_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVMET_TCP_H

#include <linux/tracepoint.h>
#include <linux/nvme.h>
#include <linux/nvme-tcp.h>

TRACE_EVENT(nvmet_tcp_try_recv_pdu,
    TP_PROTO(u8 pdu_type, u8 hdr_len, int queue_left, int qid, unsigned long long time),
    TP_ARGS(pdu_type, hdr_len, queue_left, qid, time),
    TP_STRUCT__entry(
        __field(u8, pdu_type)
        __field(u8, hdr_len)
        __field(int, queue_left)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->pdu_type = pdu_type;
        __entry->hdr_len = hdr_len;
        __entry->queue_left = queue_left;
        __entry->qid = qid;
    ),
    /** print all fields */
    TP_printk("pdu_type=%u, pdu_len=%u, remain=%d, qid=%d",
        __entry->pdu_type, __entry->hdr_len, __entry->queue_left, __entry->qid)
);

TRACE_EVENT(nvmet_tcp_done_recv_pdu,
    TP_PROTO(u16 cmd_id, int qid, bool is_write, unsigned long long time),
    TP_ARGS(cmd_id, qid, is_write, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
        __entry->is_write = is_write;
    ),
    /** print all fields */
    TP_printk("cmd_id=%u, qid=%d, is_write=%d",
        __entry->cmd_id, __entry->qid, __entry->is_write)
);

TRACE_EVENT(nvmet_tcp_exec_read_req,
    TP_PROTO(u16 cmd_id, int qid, bool is_write, unsigned long long time),
    TP_ARGS(cmd_id, qid, is_write, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
        __entry->is_write = is_write;
    ),
    TP_printk("cmd_id=%u, qid=%d, is_write=%d",
        __entry->cmd_id, __entry->qid, __entry->is_write)
);

DEFINE_EVENT(nvmet_tcp_exec_read_req, nvmet_tcp_exec_write_req,
    TP_PROTO(u16 cmd_id, int qid, bool is_write, unsigned long long time),
    TP_ARGS(cmd_id, qid, is_write, time)
);

DEFINE_EVENT(nvmet_tcp_exec_read_req, nvmet_tcp_queue_response,
    TP_PROTO(u16 cmd_id, int qid, bool is_write, unsigned long long time),
    TP_ARGS(cmd_id, qid, is_write, time)
);

TRACE_EVENT(nvmet_tcp_setup_c2h_data_pdu,
    TP_PROTO(u16 cmd_id, int qid, unsigned long long time),
    TP_ARGS(cmd_id, qid, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
    ),
    TP_printk("cmd_id=%u, qid=%d",
        __entry->cmd_id, __entry->qid)
);


DEFINE_EVENT(nvmet_tcp_setup_c2h_data_pdu, nvmet_tcp_setup_r2t_pdu,
    TP_PROTO(u16 cmd_id, int qid, unsigned long long time),
    TP_ARGS(cmd_id, qid, time)
);

DEFINE_EVENT(nvmet_tcp_setup_c2h_data_pdu, nvmet_tcp_setup_response_pdu,
    TP_PROTO(u16 cmd_id, int qid, unsigned long long time),
    TP_ARGS(cmd_id, qid, time)
);

TRACE_EVENT(nvmet_tcp_try_send_data_pdu,
    TP_PROTO(u16 cmd_id, int qid, int cp_len, int left, unsigned long long time),
    TP_ARGS(cmd_id, qid,cp_len, left, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, cp_len)
        __field(int, left)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
        __entry->cp_len = cp_len;
        __entry->left = left;
    ),
    TP_printk("cmd_id=%u, qid=%d, cp_len=%d, left=%d",
        __entry->cmd_id, __entry->qid, __entry->cp_len, __entry->left)
);

DEFINE_EVENT(nvmet_tcp_try_send_data_pdu, nvmet_tcp_try_send_r2t,
    TP_PROTO(u16 cmd_id, int qid, int cp_len, int left, unsigned long long time),
    TP_ARGS(cmd_id, qid, cp_len, left, time)
);

DEFINE_EVENT(nvmet_tcp_try_send_data_pdu, nvmet_tcp_try_send_response,
    TP_PROTO(u16 cmd_id, int qid, int cp_len, int left, unsigned long long time),
    TP_ARGS(cmd_id, qid, cp_len, left, time)
);

TRACE_EVENT(nvmet_tcp_try_send_data,
    TP_PROTO(u16 cmd_id, int qid, int cp_len,  unsigned long long time),
    TP_ARGS(cmd_id, qid, cp_len, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, cp_len)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
        __entry->cp_len = cp_len;
    ),
    TP_printk("cmd_id=%u, qid=%d, cp_len=%d",
        __entry->cmd_id, __entry->qid, __entry->cp_len)
);

TRACE_EVENT( nvmet_tcp_handle_h2c_data_pdu,
    TP_PROTO(u16 cmd_id, int qid, int datalen, unsigned long long time),
    TP_ARGS(cmd_id, qid, datalen, time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, datalen)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd_id;
        __entry->qid = qid;
        __entry->datalen = datalen;
    ),
    TP_printk("cmd_id=%u, qid=%d, datalen=%d",
        __entry->cmd_id, __entry->qid, __entry->datalen)
);

DEFINE_EVENT(nvmet_tcp_handle_h2c_data_pdu, nvmet_tcp_try_recv_data,
    TP_PROTO(u16 cmd_id, int qid, int datalen, unsigned long long time),
    TP_ARGS(cmd_id, qid, datalen, time)
);


#endif /* _TRACE_NVMET_TCP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nvmet_tcp
#include <trace/define_trace.h>