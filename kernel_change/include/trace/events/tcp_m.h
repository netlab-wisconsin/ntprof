// #undef TRACE_SYSTEM
// #define TRACE_SYSTEM tcp_m

// #if !defined(_TRACE_TCP_M_H) || defined(TRACE_HEADER_MULTI_READ)
// #define _TRACE_TCP_M_H

// #include <linux/tracepoint.h>

// DECLARE_EVENT_CLASS(tcp_cwnd_change,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time),
//     TP_STRUCT__entry(
//         __field(u32, old_cwnd)
//         __field(u32, new_cwnd)
//         __field(u32, local_port)
//         __field(u32, remote_port)
//     ),
//     TP_fast_assign(
//         __entry->old_cwnd = old_cwnd;
//         __entry->new_cwnd = new_cwnd;
//         __entry->local_port = local_port;
//         __entry->remote_port = remote_port;
//     ),
//     TP_printk("old_cwnd=%u, new_cwnd=%u, local_port=%u, remote_port=%u",
//         __entry->old_cwnd, __entry->new_cwnd, __entry->local_port, __entry->remote_port)
// )

// /** in tcp_cong.c */
// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_slow_start,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_cong_avoid_ai,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// /** in tcp_input.c */
// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_enter_loss,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_undo_cwnd_reduction,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );


// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_cwnd_reduction,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_end_cwnd_reduction,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_mtup_probe_success,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_mtup_probe_failed,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_init_transfer,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_cwnd_restart,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_cwnd_application_limited,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_cwnd_change, cwnd_tcp_mtu_probe,
//     TP_PROTO(u32 old_cwnd, u32 new_cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(old_cwnd, new_cwnd, local_port, remote_port, time)
// );

// DECLARE_EVENT_CLASS(tcp_packet_change, 
//     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time),
//     TP_STRUCT__entry(
//         __field(u32, packeg_in_flight)
//         __field(u32, cwnd)
//         __field(u32, local_port)
//         __field(u32, remote_port)
//     ),
//     TP_fast_assign(
//         __entry->packeg_in_flight = packeg_in_flight;
//         __entry->cwnd = cwnd;
//         __entry->local_port = local_port;
//         __entry->remote_port = remote_port;
//     ),
//     TP_printk("packeg_in_flight=%u, cwnd=%u, local_port=%u, remote_port=%u",
//         __entry->packeg_in_flight, __entry->cwnd, __entry->local_port, __entry->remote_port)
// )


// TRACE_EVENT(pkt_tcp_event_new_data_sent,
//     TP_PROTO(u32 packeg_in_flight, u32 skb_len, u32 pk_num, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, skb_len, pk_num, cwnd, local_port, remote_port, time),
//     TP_STRUCT__entry(
//         __field(u32, packeg_in_flight)
//         __field(u32, cwnd)
//         __field(u32, local_port)
//         __field(u32, remote_port)
//     ),
//     TP_fast_assign(
//         __entry->packeg_in_flight = packeg_in_flight;
//         __entry->cwnd = cwnd;
//         __entry->local_port = local_port;
//         __entry->remote_port = remote_port;
//     ),
//     TP_printk("packeg_in_flight=%u, cwnd=%u, local_port=%u, remote_port=%u",
//         __entry->packeg_in_flight, __entry->cwnd, __entry->local_port, __entry->remote_port)

// );

// // DEFINE_EVENT(tcp_packet_change, pkt_tcp_event_new_data_sent,
// //     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
// //     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time)
// // );

// DEFINE_EVENT(tcp_packet_change, pkt_tcp_connect_queue_skb,
//     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_packet_change, pkt_tcp_clean_rtx_queue,
//     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_packet_change, pkt_tcp_adjust_pcount,
//     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time)
// );

// DEFINE_EVENT(tcp_packet_change, pkt_tcp_send_syn_data,
//     TP_PROTO(u32 packeg_in_flight, u32 cwnd, u32 local_port, u32 remote_port, u64 time),
//     TP_ARGS(packeg_in_flight, cwnd, local_port, remote_port, time)
// );

// #endif /* _TRACE_TCP_M_H */

// #undef TRACE_INCLUDE_PATH
// #define TRACE_INCLUDE_PATH trace/events
// #undef TRACE_INCLUDE_FILE
// #define TRACE_INCLUDE_FILE tcp_m
// #include <trace/define_trace.h>