#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvmet_tcp

#if !defined(_TRACE_NVMET_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVMET_TCP_H

#include <linux/tracepoint.h>
#include <linux/nvme.h>
#include <linux/nvme-tcp.h>

TRACE_EVENT(nvmet_tcp_done_recv_pdu,
    TP_PROTO(struct nvme_tcp_cmd_pdu *pdu, int qid, long long recv_time),
    TP_ARGS(pdu, qid, recv_time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(__u8, opcode)
    ),
    TP_fast_assign(
        __entry->cmd_id = pdu->cmd.common.command_id;
        __entry->opcode = pdu->cmd.common.opcode;
    ),
    /** print all fields */
    TP_printk("cmd_id=%u, qid=%d, opcode=%d",
        __entry->cmd_id, __entry->qid, __entry->opcode)
);

TRACE_EVENT(nvmet_tcp_exec_read_req,
    TP_PROTO(struct nvme_command *cmd, int qid),
    TP_ARGS(cmd, qid),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(u8, opcode)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd->common.command_id;
        __entry->qid = qid;
        __entry->opcode = cmd->common.opcode;
    ),
    TP_printk("cmd_id=%u, qid=%d, opcode=%d",
        __entry->cmd_id, __entry->qid, __entry->opcode)
);

TRACE_EVENT(nvmet_tcp_exec_write_req,
    TP_PROTO(struct nvme_command *cmd, int qid, int size),
    TP_ARGS(cmd, qid, size),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(u8, opcode)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd->common.command_id;
        __entry->qid = qid;
        __entry->opcode = cmd->common.opcode;
    ),
    TP_printk("cmd_id=%u, qid=%d, opcode=%d",
        __entry->cmd_id, __entry->qid, __entry->opcode)
);

TRACE_EVENT(nvmet_tcp_queue_response,
    TP_PROTO(struct nvme_command *cmd, int qid),
    TP_ARGS(cmd, qid),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(bool, is_write)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd->common.command_id;
        __entry->qid = qid;
        __entry->is_write = nvme_is_write(cmd);
    ),
    TP_printk("cmd_id=%u, qid=%d, is_write=%d",
        __entry->cmd_id, __entry->qid, __entry->is_write)
);

TRACE_EVENT(nvmet_tcp_setup_c2h_data_pdu,
    TP_PROTO(struct nvme_completion	*cqe, int qid),
    TP_ARGS(cqe, qid),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->cmd_id = cqe->command_id;
        __entry->qid = qid;
    ),
    TP_printk("cmd_id=%u, qid=%d",
        __entry->cmd_id, __entry->qid)
);

DEFINE_EVENT(nvmet_tcp_queue_response, nvmet_tcp_setup_r2t_pdu,
    TP_PROTO(struct nvme_command *cmd, int qid),
    TP_ARGS(cmd, qid)
);

DEFINE_EVENT(nvmet_tcp_setup_c2h_data_pdu, nvmet_tcp_setup_response_pdu,
    TP_PROTO(struct nvme_completion	*cqe, int qid),
    TP_ARGS(cqe, qid)
);

TRACE_EVENT(nvmet_tcp_try_send_data_pdu,
    TP_PROTO(struct nvme_completion	*cqe, void *pdu, int qid, int size),
    TP_ARGS(cqe, pdu, qid, size),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->cmd_id = cqe->command_id;
        __entry->qid = qid;
    ),
    TP_printk("cmd_id=%u, qid=%d",
        __entry->cmd_id, __entry->qid)
);

TRACE_EVENT(nvmet_tcp_try_send_r2t,
  TP_PROTO(struct nvme_command	*cmd, void *pdu, int qid, int size),
  TP_ARGS(cmd, pdu, qid, size),
  TP_STRUCT__entry(
      __field(u16, cmd_id)
      __field(int, qid)
  ),
  TP_fast_assign(
      __entry->cmd_id = cmd->common.command_id;
      __entry->qid = qid;
  ),
  TP_printk("cmd_id=%u, qid=%d",
      __entry->cmd_id, __entry->qid)
);

TRACE_EVENT(nvmet_tcp_try_send_response,
    TP_PROTO(struct nvme_completion	*cqe,void *pdu, int qid, int size),
    TP_ARGS(cqe, pdu, qid, size),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
    ),
    TP_fast_assign(
        __entry->cmd_id = cqe->command_id;
        __entry->qid = qid;
    ),
    TP_printk("cmd_id=%u, qid=%d",
        __entry->cmd_id, __entry->qid)
);

TRACE_EVENT(nvmet_tcp_try_send_data,
    TP_PROTO(struct nvme_completion	*cqe, int qid, int size),
    TP_ARGS(cqe, qid, size),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, size)
    ),
    TP_fast_assign(
        __entry->cmd_id = cqe->command_id;
        __entry->qid = qid;
        __entry->size = size;
    ),
    TP_printk("cmd_id=%u, qid=%d, size=%d",
        __entry->cmd_id, __entry->qid, __entry->size)
);

TRACE_EVENT(nvmet_tcp_handle_h2c_data_pdu,
    TP_PROTO(struct nvme_tcp_data_pdu *pdu, struct nvme_command *cmd, int qid, int size, long long recv_time),
    TP_ARGS(pdu, cmd, qid, size, recv_time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, size)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd->common.command_id;
        __entry->qid = qid;
        __entry->size = size;
    ),
    TP_printk("cmd_id=%u, qid=%d, size=%d",
        __entry->cmd_id, __entry->qid, __entry->size)
);

TRACE_EVENT(nvmet_tcp_try_recv_data,
    TP_PROTO(struct nvme_command *cmd, int qid, int size, long long recv_time),
    TP_ARGS(cmd, qid, size, recv_time),
    TP_STRUCT__entry(
        __field(u16, cmd_id)
        __field(int, qid)
        __field(int, size)
    ),
    TP_fast_assign(
        __entry->cmd_id = cmd->common.command_id;
        __entry->qid = qid;
        __entry->size = size;
    ),
    TP_printk("cmd_id=%u, qid=%d, size=%d",
        __entry->cmd_id, __entry->qid, __entry->size)
);

#endif /* _TRACE_NVMET_TCP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nvmet_tcp
#include <trace/define_trace.h>