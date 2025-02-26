#include "trace_nvmet_tcp.h"

#include <linux/slab.h>
#include <linux/tracepoint.h>
#include <trace/events/nvmet_tcp.h>
#include <linux/nvme-tcp.h>

#include "target.h"
#include "../include/statistics.h"
#include "target_logging.h"

#define LOCK_QUEUE(qid) \
    SPINLOCK_IRQSAVE_DISABLEPREEMPT_DISABLEBH(&stat[qid].lock, __func__)

#define UNLOCK_QUEUE(qid) \
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT_ENABLEBH(&stat[qid].lock, __func__)

static struct profile_record *create_record(u16 cmdid, int size, bool is_write)
{
    struct profile_record *record = kmalloc(sizeof(*record), GFP_ATOMIC);
    if (!record) {
        pr_err("Allocation failed for cmd %u\n", cmdid);
        return NULL;
    }
    init_profile_record(record, size, is_write, "", cmdid);
    return record;
}

// fetch a profile record and append the time-event pair
#define HANDLE_COMMON_EVENT(qid, cmdid, time, event) \
    do { \
        struct profile_record *___record; \
        LOCK_QUEUE(qid); \
        ___record = get_profile_record(&stat[(qid)], (cmdid)); \
        if (___record) \
            append_event(___record, (time), (event)); \
        UNLOCK_QUEUE(qid); \
    } while (0)

void on_done_recv_pdu(void *ignore, struct nvme_tcp_cmd_pdu *pdu, int qid, long long recv_time) {
    if (!pdu->stat.tag)
        return;

    u16 cmdid = pdu->cmd.common.command_id;
    u8 opcode = pdu->cmd.common.opcode;
    int size = le32_to_cpu(pdu->cmd.common.dptr.sgl.length);
    u64 now = ktime_get_real_ns();
    bool is_write;
    struct profile_record *record;

    if (opcode == nvme_cmd_read) {
        is_write = false;
    } else if (opcode == nvme_cmd_write) {
        is_write = true;
    } else {
        pr_warn("Unexpected opcode %u on qid %d\n", opcode, qid);
        return;
    }

    //TODO: manually release the memory
    if (!(record = create_record(cmdid, size, is_write)))
        return;

    append_event(record, recv_time, NVMET_TCP_TRY_RECV_PDU);
    append_event(record, now, NVMET_TCP_DONE_RECV_PDU);

    LOCK_QUEUE(qid);
    if (unlikely(get_profile_record(&stat[qid], cmdid))) {
        pr_warn("Duplicate cmd %u on qid %d\n", cmdid, qid);
        kfree(record);
    } else {
        append_record(&stat[qid], record);
    }
    UNLOCK_QUEUE(qid);
}

void on_exec_read_req(void *ignore, struct nvme_command *cmd, int qid) {
    u16 cmdid = cmd->common.command_id;
    u8 opcode = cmd->common.opcode;
    u64 now = ktime_get_real_ns();

    if (opcode != nvme_cmd_read && opcode != 24) {
        pr_warn("Invalid opcode in on_exec_read_req: %d\n", opcode);
        return;
    }
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_EXEC_READ_REQ);
}



void on_exec_write_req(void *ignore, struct nvme_command *cmd, int qid, int size) {
    u16 cmdid = cmd->common.command_id;
    u8 opcode = cmd->common.opcode;
    u64 now = ktime_get_real_ns();

    if (opcode != nvme_cmd_write) {
        pr_err("Invalid opcode in on_exec_write_req: %d\n", opcode);
        return;
    }
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_EXEC_WRITE_REQ);
}

void on_queue_response(void *ignore, struct nvme_command *cmd, int qid) {
    u16 cmdid = cmd->common.command_id;
    u64 now = ktime_get_real_ns();

    struct profile_record *record;
    SPINLOCK_IRQSAVE_DISABLEPREEMPT(&stat[qid].lock, __func__);
    record = get_profile_record(&stat[qid], cmdid);
    if (record) {
        append_event(record, now, NVMET_TCP_QUEUE_RESPONSE);
    }
    SPINUNLOCK_IRQRESTORE_ENABLEPREEMPT(&stat[qid].lock, __func__);
}

void on_setup_c2h_data_pdu(void *ignore, struct nvme_completion	*cqe, int qid) {
    u16 cmdid = cqe->command_id;
    u64 now = ktime_get_real_ns();
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_SETUP_C2H_DATA_PDU);
}

void on_setup_r2t_pdu(void *ignore, struct nvme_command *cmd, int qid) {
    u16 cmdid = cmd->common.command_id;
    u64 now = ktime_get_real_ns();
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_SETUP_R2T_PDU);
}

void on_setup_response_pdu(void *ignore, struct nvme_completion	*cqe, int qid) {
    u16 cmdid = cqe->command_id;
    u64 now = ktime_get_real_ns();
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_SETUP_RESPONSE_PDU);
}

void cpy_stat(struct profile_record *record, struct ntprof_stat *s) {
    s->cnt = 0;
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

void on_try_send_data_pdu(void *ignore, struct nvme_completion	*cqe, void *pdu, int qid, int size) {
    u16 cmd_id = cqe->command_id;
    u64 now = ktime_get_real_ns();
    HANDLE_COMMON_EVENT(qid, cmd_id, now, NVMET_TCP_TRY_SEND_DATA_PDU);
}

static void process_and_remove_record(int qid, u16 cmdid, void *pdu, enum EEvent event)
{
    struct list_head *pos, *n;
    struct profile_record *record;
    u64 now = ktime_get_real_ns();

    LOCK_QUEUE(qid);
    list_for_each_safe(pos, n, &stat[qid].records) {
        record = list_entry(pos, struct profile_record, list);
        if (record->metadata.req_tag == cmdid) {
            append_event(record, now, event);
            cpy_stat(record, &((struct nvme_tcp_data_pdu *)pdu)->stat);
            list_del_init(pos);
            free_profile_record(record);
            break;
        }
    }
    UNLOCK_QUEUE(qid);
}

void on_try_send_r2t(void *ignore, struct nvme_command	*cmd, void *pdu, int qid, int size) {
    process_and_remove_record(qid, cmd->common.command_id, pdu, NVMET_TCP_TRY_SEND_R2T);
}

void on_try_send_response(void *ignore, struct nvme_completion	*cqe,void *pdu, int qid, int size) {
    process_and_remove_record(qid, cqe->command_id, pdu, NVMET_TCP_TRY_SEND_RESPONSE);
}

void on_try_send_data(void *ignore, struct nvme_completion	*cqe, int qid, int size) {
    u16 cmdid = cqe->command_id;
    u64 now = ktime_get_real_ns();
    HANDLE_COMMON_EVENT(qid, cmdid, now, NVMET_TCP_TRY_SEND_DATA);
}

void on_try_recv_data(void *ignore, struct nvme_command	*cmd, int qid, int size, long long recv_time) {
    u16 cmdid = cmd->common.command_id;
    HANDLE_COMMON_EVENT(qid, cmdid, recv_time, NVMET_TCP_TRY_RECV_DATA);
}

void on_handle_h2c_data_pdu(void *ignore, struct nvme_tcp_data_pdu *pdu, struct nvme_command	*cmd, int qid, int size, long long recv_time) {
    if (!pdu->stat.tag) return;
    u16 cmd_id = cmd->common.command_id;
    u64 now = ktime_get_real_ns();

    struct profile_record *record;

    if (!(record = create_record(cmd_id, size, true))) {
        return;
    }

    append_event(record, recv_time, NVMET_TCP_TRY_RECV_PDU);
    append_event(record, now, NVMET_TCP_HANDLE_H2C_DATA_PDU);

    LOCK_QUEUE(qid);
    if (unlikely(get_profile_record(&stat[qid], cmd_id))) {
        pr_warn("Duplicate cmd %u on qid %d\n", cmd_id, qid);
        kfree(record);
    } else {
        append_record(&stat[qid], record);
    }
    UNLOCK_QUEUE(qid);
}


typedef void (*nvmet_tcp_trace_handler_t)(void *context, ...);

typedef int (*trace_register_fn_t)(nvmet_tcp_trace_handler_t, void *);
typedef void (*trace_unregister_fn_t)(nvmet_tcp_trace_handler_t, void *);

struct nvmet_tcp_tracepoint {
    trace_register_fn_t register_fn;
    trace_unregister_fn_t unregister_fn;
    nvmet_tcp_trace_handler_t handler;
};

#define TRACEPOINT_ENTRY(name) \
{ \
.register_fn = (trace_register_fn_t)register_trace_nvmet_tcp_##name, \
.unregister_fn = (trace_unregister_fn_t)unregister_trace_nvmet_tcp_##name, \
.handler = (nvmet_tcp_trace_handler_t)on_##name \
}

static struct nvmet_tcp_tracepoint tracepoints[] = {
    TRACEPOINT_ENTRY(done_recv_pdu),
    TRACEPOINT_ENTRY(exec_read_req),
    TRACEPOINT_ENTRY(exec_write_req),
    TRACEPOINT_ENTRY(queue_response),
    TRACEPOINT_ENTRY(setup_c2h_data_pdu),
    TRACEPOINT_ENTRY(setup_r2t_pdu),
    TRACEPOINT_ENTRY(setup_response_pdu),
    TRACEPOINT_ENTRY(try_send_data_pdu),
    TRACEPOINT_ENTRY(try_send_r2t),
    TRACEPOINT_ENTRY(try_send_response),
    TRACEPOINT_ENTRY(try_send_data),
    TRACEPOINT_ENTRY(handle_h2c_data_pdu),
    TRACEPOINT_ENTRY(try_recv_data),
};

int register_nvmet_tcp_tracepoints(void)
{
    int ret;
    int registered = 0;

    for (; registered < ARRAY_SIZE(tracepoints); registered++) {
        struct nvmet_tcp_tracepoint *t = &tracepoints[registered];

        ret = t->register_fn(t->handler, NULL);
        if (ret) {
            pr_err("Failed to register tracepoint %d (handler: %ps)\n",
                  registered, t->handler);
            goto unregister;
        }
    }
    return 0;

    unregister:
        while (--registered >= 0) {
            tracepoints[registered].unregister_fn(tracepoints[registered].handler, NULL);
        }
    return ret;
}

void unregister_nvmet_tcp_tracepoints(void)
{
    int i;

    for (i = ARRAY_SIZE(tracepoints) - 1; i >= 0; i--) {
        tracepoints[i].unregister_fn(tracepoints[i].handler, NULL);
    }
}


// typedef int (*trace_register_fn_t)(void (*)(void *, ...), void *);
// typedef void (*trace_unregister_fn_t)(void (*)(void *, ...), void *);
//
// struct nvmet_tcp_tracepoint {
//     trace_register_fn_t register_fn;
//     trace_unregister_fn_t unregister_fn;
//     void (*handler)(void *, ...); // Use variadic function pointer to match different handlers
// };
//
//
// static struct nvmet_tcp_tracepoint tracepoints[] = {
//     { register_trace_nvmet_tcp_done_recv_pdu, unregister_trace_nvmet_tcp_done_recv_pdu, on_done_recv_pdu },
//     { register_trace_nvmet_tcp_exec_read_req, unregister_trace_nvmet_tcp_exec_read_req, on_exec_read_req },
//     { register_trace_nvmet_tcp_exec_write_req, unregister_trace_nvmet_tcp_exec_write_req, on_exec_write_req },
//     { register_trace_nvmet_tcp_queue_response, unregister_trace_nvmet_tcp_queue_response, on_queue_response },
//     { register_trace_nvmet_tcp_setup_c2h_data_pdu, unregister_trace_nvmet_tcp_setup_c2h_data_pdu, on_setup_c2h_data_pdu },
//     { register_trace_nvmet_tcp_setup_r2t_pdu, unregister_trace_nvmet_tcp_setup_r2t_pdu, on_setup_r2t_pdu },
//     { register_trace_nvmet_tcp_setup_response_pdu, unregister_trace_nvmet_tcp_setup_response_pdu, on_setup_response_pdu },
//     { register_trace_nvmet_tcp_try_send_data_pdu, unregister_trace_nvmet_tcp_try_send_data_pdu, on_try_send_data_pdu },
//     { register_trace_nvmet_tcp_try_send_r2t, unregister_trace_nvmet_tcp_try_send_r2t, on_try_send_r2t },
//     { register_trace_nvmet_tcp_try_send_response, unregister_trace_nvmet_tcp_try_send_response, on_try_send_response },
//     { register_trace_nvmet_tcp_try_send_data, unregister_trace_nvmet_tcp_try_send_data, on_try_send_data },
//     { register_trace_nvmet_tcp_handle_h2c_data_pdu, unregister_trace_nvmet_tcp_handle_h2c_data_pdu, on_handle_h2c_data_pdu },
//     { register_trace_nvmet_tcp_try_recv_data, unregister_trace_nvmet_tcp_try_recv_data, on_try_recv_data },
// };
//
// int register_nvmet_tcp_tracepoints(void) {
//     int ret = 0;
//     int registered = 0;
//
//     for (; registered < ARRAY_SIZE(tracepoints); registered++) {
//         ret = tracepoints[registered].register_fn(tracepoints[registered].handler, NULL);
//         if (ret) {
//             pr_err("Failed to register tracepoint %d\n", registered);
//             goto unregister;
//         }
//     }
//
//     return 0;
//
//     unregister:
//         while (--registered >= 0) {
//             tracepoints[registered].unregister_fn(tracepoints[registered].handler, NULL);
//         }
//     return ret;
// }
//
// void unregister_nvmet_tcp_tracepoints(void) {
//     int i;
//
//     for (i = ARRAY_SIZE(tracepoints) - 1; i >= 0; i--) {
//         tracepoints[i].unregister_fn(tracepoints[i].handler, NULL);
//     }
// }


// int register_nvmet_tcp_tracepoints(void) {
//     int ret;
//
//     ret = register_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
//     if (ret) goto failed;
//
//     ret = register_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
//     if (ret) goto unregister_done_recv_pdu;
//
//     ret = register_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
//     if (ret) goto unregister_exec_read_req;
//
//     ret = register_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
//     if (ret) goto unregister_exec_write_req;
//
//     ret = register_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
//     if (ret) goto unregister_queue_response;
//
//     ret = register_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
//     if (ret) goto unregister_setup_c2h_data_pdu;
//
//     ret = register_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
//     if (ret) goto unregister_setup_r2t_pdu;
//
//     ret = register_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
//     if (ret) goto unregister_setup_response_pdu;
//
//     ret = register_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
//     if (ret) goto unregister_try_send_data_pdu;
//
//     ret = register_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
//     if (ret) goto unregister_try_send_r2t;
//
//     ret = register_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
//     if (ret) goto unregister_try_send_response;
//
//     ret = register_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
//     if (ret) goto unregister_try_send_data;
//
//     ret = register_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
//     if (ret) goto unregister_handle_h2c_data_pdu;
//
//     return 0;
//
// unregister_try_recv_data:
//     unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
// unregister_handle_h2c_data_pdu:
//     unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
// unregister_try_send_data:
//     unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
// unregister_try_send_response:
//     unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
// unregister_try_send_r2t:
//     unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
// unregister_try_send_data_pdu:
//     unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
// unregister_setup_response_pdu:
//     unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
// unregister_setup_r2t_pdu:
//     unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
// unregister_setup_c2h_data_pdu:
//     unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
// unregister_queue_response:
//     unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
// unregister_exec_write_req:
//     unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
// unregister_exec_read_req:
//     unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
// unregister_done_recv_pdu:
//     unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
// failed:
//     pr_info("failed to register tracepoints\n");
//     return ret;
// }
//
// void unregister_nvmet_tcp_tracepoints(void) {
//   pr_info("unregister tracepoints\n");
//   unregister_trace_nvmet_tcp_done_recv_pdu(on_done_recv_pdu, NULL);
//   unregister_trace_nvmet_tcp_exec_read_req(on_exec_read_req, NULL);
//   unregister_trace_nvmet_tcp_exec_write_req(on_exec_write_req, NULL);
//   unregister_trace_nvmet_tcp_queue_response(on_queue_response, NULL);
//   unregister_trace_nvmet_tcp_setup_c2h_data_pdu(on_setup_c2h_data_pdu, NULL);
//   unregister_trace_nvmet_tcp_setup_r2t_pdu(on_setup_r2t_pdu, NULL);
//   unregister_trace_nvmet_tcp_setup_response_pdu(on_setup_response_pdu, NULL);
//   unregister_trace_nvmet_tcp_try_send_data_pdu(on_try_send_data_pdu, NULL);
//   unregister_trace_nvmet_tcp_try_send_r2t(on_try_send_r2t, NULL);
//   unregister_trace_nvmet_tcp_try_send_response(on_try_send_response, NULL);
//   unregister_trace_nvmet_tcp_try_send_data(on_try_send_data, NULL);
//   unregister_trace_nvmet_tcp_handle_h2c_data_pdu(on_handle_h2c_data_pdu, NULL);
//   unregister_trace_nvmet_tcp_try_recv_data(on_try_recv_data, NULL);
// }

