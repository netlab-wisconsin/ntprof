#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>


/*
node0:~> sudo cat /sys/kernel/tracing/events/nvme_tcp/nvme_tcp_queue_rq/format
name: nvme_tcp_queue_rq
ID: 1939
format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:char disk[32];	offset:8;	size:32;	signed:1;
	field:int ctrl_id;	offset:40;	size:4;	signed:1;
	field:int qid;	offset:44;	size:4;	signed:1;
	field:int cid;	offset:48;	size:4;	signed:1;
	field:u64 slba;	offset:56;	size:8;	signed:0;
	field:u64 pos;	offset:64;	size:8;	signed:0;
	field:u32 length;	offset:72;	size:4;	signed:0;
	field:int r_tag;	offset:76;	size:4;	signed:1;
	field:bool is_write;	offset:80;	size:1;	signed:0;

print fmt: "nvme%d: %sqid=%d, rtag=%d, cmdid=%u, slba=%llu, pos=%llu, length=%u, is_write=%d", REC->ctrl_id, nvme_tcp_trace_disk_name(p, REC->disk), REC->qid, REC->r_tag, REC->cid, REC->slba, REC->pos, REC->length, REC->is_write
*/
// Manually define the struct based on TRACE_EVENT(nvme_tcp_queue_rq)

struct header {
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;
};

struct trace_event_raw_nvme_tcp_queue_rq {
    struct header h;
    char disk[32];       // Disk name (assuming DISK_NAME_LEN is 32)
    int ctrl_id;         // Controller ID
    int qid;             // Queue ID
    int cid;             // Command ID
    __u64 slba;          // Start LBA
    __u64 pos;           // Position
    __u32 length;        // Length of request
    int r_tag;           // Request tag
    bool is_write;       // Is write operation or not
};

SEC("tracepoint/nvme_tcp/nvme_tcp_queue_rq")
int tracepoint__nvme_tcp_queue_rq(struct trace_event_raw_nvme_tcp_queue_rq *ctx) {
    // eBPF program
    // append the event to the profiling record
    return 0;
}

struct trace_event_raw_nvme_tcp_queue_request {

}


char _license[] SEC("license") = "GPL";


// // // 定义 GPL 许可证
// #include <vmlinux.h>
// #include <bpf/bpf_helpers.h>
// #include <bpf/bpf_tracing.h>

// struct v {
//   __u32 a;
// };

// struct {
//   __uint(type, BPF_MAP_TYPE_HASH);
//   __uint(max_entries, 1024);
//   __type(key, __u32);
//   __type(value, struct v);
// } events SEC(".maps");

// // tracepoint 回调函数
// SEC("tracepoint/block/block_rq_complete")
// int tracepoint__block_rq_complete(struct request *rq, int error, unsigned int nr_bytes)
// {
//     __u32 key = 0;  // 我们使用一个简单的标识符来记录调用次数，这里使用 key = 0
//     struct v *value;
//     struct v v1 = {1};

//     bpf_printk("block_rq_complete: req->tag=%d, nr_bytes=%u\n", rq->tag, nr_bytes);

//     // 从 BPF map 中获取当前的调用次数
//     value = bpf_map_lookup_elem(&events, &key);
//     if (value) {
//         value->a++;
//     } else {
//         bpf_map_update_elem(&events, &key, &v1, BPF_ANY);
//     }
//     return 0;
// }

// // 定义 GPL 许可证
// char LICENSE[] SEC("license") = "Dual BSD/GPL";


// to show the bpf_printk output
// sudo cat /sys/kernel/debug/tracing/trace_pipe

// run and compile eBPF program
// ./ecc nvme_tcp_trace.bpf.c
// sudo ./ecli run package.json