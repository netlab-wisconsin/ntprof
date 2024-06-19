#define CREATE_TRACE_POINTS
#include <trace/events/tcp_m.h>

#include <linux/module.h> 
#include <linux/tracepoint.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_slow_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_cong_avoid_ai);

EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_enter_loss);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_undo_cwnd_reduction);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_cwnd_reduction);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_end_cwnd_reduction);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_mtup_probe_success);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_mtup_probe_failed);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_init_transfer);

EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_cwnd_restart);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_cwnd_application_limited);
EXPORT_TRACEPOINT_SYMBOL_GPL(cwnd_tcp_mtu_probe);

EXPORT_TRACEPOINT_SYMBOL_GPL(pkt_tcp_event_new_data_sent);
EXPORT_TRACEPOINT_SYMBOL_GPL(pkt_tcp_connect_queue_skb);
EXPORT_TRACEPOINT_SYMBOL_GPL(pkt_tcp_clean_rtx_queue);
EXPORT_TRACEPOINT_SYMBOL_GPL(pkt_tcp_adjust_pcount);
EXPORT_TRACEPOINT_SYMBOL_GPL(pkt_tcp_send_syn_data);