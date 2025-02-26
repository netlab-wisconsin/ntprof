#ifndef TRACE_H
#define TRACE_H

typedef void (*ntprof_trace_handler_t)(void* context, ...);

typedef int (*trace_register_fn_t)(ntprof_trace_handler_t, void*);
typedef void (*trace_unregister_fn_t)(ntprof_trace_handler_t, void*);

struct nvmf_tracepoint {
  trace_register_fn_t register_fn;
  trace_unregister_fn_t unregister_fn;
  ntprof_trace_handler_t handler;
};

#define TRACEPOINT_ENTRY(name) \
{ \
  .register_fn = (trace_register_fn_t)register_trace_nvmet_tcp_##name, \
  .unregister_fn = (trace_unregister_fn_t)unregister_trace_nvmet_tcp_##name, \
  .handler = (ntprof_trace_handler_t)on_##name \
}

#endif //TRACE_H