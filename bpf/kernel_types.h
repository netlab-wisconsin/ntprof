/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The few kernel types ntprof reads, declared for BPF CO-RE.
 *
 * Only the members that are read are declared. preserve_access_index makes
 * clang emit a relocation for each access, and libbpf resolves the offsets
 * against the running kernel's BTF (or the BTF passed with --btf), so these
 * layouts do not need to match any particular kernel version.
 *
 * Wire-format structures (NVMe commands, NVMe/TCP PDU headers) are declared
 * without preserve_access_index: their layout is fixed by the specifications.
 */
#ifndef NTPROF_KERNEL_TYPES_H
#define NTPROF_KERNEL_TYPES_H

#include <linux/types.h>

typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;
typedef __s32 s32;
typedef __s64 s64;
typedef _Bool bool;
#define true 1
#define false 0

/*
 * x86-64 register file as the kernel names it (BPF_KPROBE reads it). Defining
 * __VMLINUX_H__ tells bpf_tracing.h to use the kernel's member names (di, si,
 * ...) rather than the user-space ones (rdi, rsi, ...).
 */
#define __VMLINUX_H__
struct pt_regs {
  unsigned long r15, r14, r13, r12, bp, bx, r11, r10, r9, r8;
  unsigned long ax, cx, dx, si, di, orig_ax, ip, cs, flags, sp, ss;
};

#define __core __attribute__((preserve_access_index))

/* ---- networking ---- */

/* nvme-tcp stores its queue in sk_user_data (no flag bits). */
struct sock {
  void* sk_user_data;
} __core;

struct net_device {
  int ifindex;
} __core;

struct sk_buff {
  struct sock* sk;
  struct net_device* dev;
  s64 tstamp; /* ktime_t: receive time (CLOCK_REALTIME) when stamping is on */
} __core;

struct socket {
  struct sock* sk;
} __core;

/* ---- block layer ---- */

struct gendisk {
  char disk_name[32];
} __core;

struct request_queue {
  struct gendisk* disk;
} __core;

struct bio {
  void* bi_private;
} __core;

struct blk_mq_hw_ctx;

struct request {
  struct request_queue* q;
  struct blk_mq_hw_ctx* mq_hctx;
  unsigned int cmd_flags;
  unsigned int __data_len;
  int tag;
  u64 start_time_ns;
  struct bio* bio;
} __core;

struct blk_mq_hw_ctx {
  void* driver_data;
  unsigned int queue_num;
} __core;

struct blk_mq_queue_data {
  struct request* rq;
} __core;

/* ---- NVMe host core (drivers/nvme/host/nvme.h) ---- */

struct nvme_ctrl {
  u16 cntlid;
  int instance;
} __core;

/* The driver PDU of every NVMe request starts with this. */
struct nvme_request {
  void* cmd; /* struct nvme_command * (wire format: struct ntp_nvme_cmd) */
  struct nvme_ctrl* ctrl;
} __core;

/* ---- NVMe/TCP host (drivers/nvme/host/tcp.c) ---- */

struct nvme_tcp_queue;

/* The kernel's enum, as for nvmet-tcp below (values stable since 5.0). */
enum nvme_tcp_send_state {
  NVME_TCP_SEND_CMD_PDU = 0,
  NVME_TCP_SEND_H2C_PDU = 1,
  NVME_TCP_SEND_DATA = 2,
  NVME_TCP_SEND_DDGST = 3,
};

struct nvme_tcp_request {
  struct nvme_tcp_queue* queue;
  u32 data_len;
  enum nvme_tcp_send_state state; /* what goes out next */
} __core;

struct nvme_tcp_queue {
  struct socket* sock;
  void* pdu; /* receive buffer of the PDU header being parsed */
  struct nvme_tcp_request* request; /* request being sent; NULL when done */
} __core;

/* ---- NVMe target core (drivers/nvme/target/nvmet.h) ---- */

struct nvmet_ctrl {
  u16 cntlid;
} __core;

struct nvmet_sq {
  struct nvmet_ctrl* ctrl;
  u16 qid;
  bool sqhd_disabled;
} __core;

struct nvmet_req {
  void* cmd; /* struct nvme_command * (wire format: struct ntp_nvme_cmd) */
  struct nvmet_sq* sq;
  unsigned long transfer_len; /* size_t */
} __core;

/* ---- NVMe/TCP target (drivers/nvme/target/tcp.c) ---- */

struct work_struct {
  long data;
} __core;

struct nvmet_tcp_queue;

/*
 * CO-RE matches an enum member only against an enum of the same name, so the
 * send state is declared as the kernel's enum (values stable since 5.0).
 */
enum nvmet_tcp_send_state {
  NVMET_TCP_SEND_DATA_PDU = 0,
  NVMET_TCP_SEND_DATA = 1,
  NVMET_TCP_SEND_R2T = 2,
  NVMET_TCP_SEND_DDGST = 3,
  NVMET_TCP_SEND_RESPONSE = 4,
};

struct nvmet_tcp_cmd {
  struct nvmet_tcp_queue* queue;
  struct nvmet_req req;
  u32 rbytes_done; /* data bytes received from the host */
  u32 offset;      /* bytes of the current PDU already sent */
  enum nvmet_tcp_send_state state;
} __core;

/* The receive state, likewise (values stable since 5.0). */
enum nvmet_tcp_recv_state {
  NVMET_TCP_RECV_PDU = 0,
  NVMET_TCP_RECV_DATA = 1,
  NVMET_TCP_RECV_DDGST = 2,
  NVMET_TCP_RECV_ERR = 3,
};

struct nvmet_tcp_queue {
  struct socket* sock;
  struct work_struct io_work;
  struct nvmet_sq nvme_sq;
  struct nvmet_tcp_cmd* cmd; /* the command whose PDU or data is being received */
  enum nvmet_tcp_recv_state rcv_state;
  struct nvmet_tcp_cmd* snd_cmd;
} __core;

/* ---- RDMA verbs (include/rdma/ib_verbs.h) ---- */

struct ib_cqe {
  void* done;
} __core;

struct ib_qp {
  void* qp_context; /* the ULP's queue (nvme_rdma_queue, nvmet_rdma_queue) */
} __core;

/* wr_cqe shares a union with wr_id; CO-RE finds it by name */
struct ib_wc {
  struct ib_cqe* wr_cqe;
  struct ib_qp* qp;
} __core;

/* ---- NVMe/RDMA host (drivers/nvme/host/rdma.c) ---- */

struct nvme_rdma_qe {
  struct ib_cqe cqe; /* first: wc->wr_cqe points at the qe */
  void* data;        /* the command, or the received completion */
} __core;

struct nvme_rdma_queue;

struct nvme_rdma_request {
  struct nvme_rdma_qe sqe;
  struct nvme_rdma_queue* queue;
} __core;

/* ---- NVMe/RDMA target (drivers/nvme/target/rdma.c) ---- */

struct nvmet_rdma_rsp {
  struct ib_cqe send_cqe;
  struct ib_cqe read_cqe;
  struct nvmet_req req;
  u32 flags; /* NVMET_RDMA_REQ_INLINE_DATA */
} __core;

#define NVMET_RDMA_REQ_INLINE_DATA (1 << 0)

/*
 * ---- wire formats (NVMe base spec, NVMe/TCP transport spec) ----
 *
 * These carry their own names so that no CO-RE relocation is ever emitted
 * for them (the kernel's struct nvme_command is a union of command layouts).
 */

/* struct nvme_common_command / nvme_rw_command, 64 bytes */
struct ntp_nvme_cmd {
  u8 opcode;
  u8 flags;
  u16 command_id;
  u32 nsid;
  u32 cdw2[2];
  u64 metadata;
  u64 dptr[2];
  u64 slba;    /* cdw10-11 */
  u16 length;  /* cdw12[15:0], 0's based block count */
  u16 control; /* cdw12[31:16] */
  u32 cdw13;
  u32 cdw14;
  u32 cdw15;
};

struct ntp_tcp_hdr {
  u8 type;
  u8 flags;
  u8 hlen;
  u8 pdo;
  u32 plen;
};

enum {
  nvme_tcp_cmd = 0x4,
  nvme_tcp_rsp = 0x5,
  nvme_tcp_h2c_data = 0x6,
  nvme_tcp_c2h_data = 0x7,
  nvme_tcp_r2t = 0x9,
};

#define NVME_TCP_F_DATA_SUCCESS (1 << 3)

/* C2H / H2C data PDU header */
struct ntp_tcp_data_pdu {
  struct ntp_tcp_hdr hdr;
  u16 command_id;
  u16 ttag;
  u32 data_offset;
  u32 data_length;
  u8 rsvd[4];
};

/* Completion queue entry, 16 bytes (NVMe base spec) */
struct ntp_nvme_cqe {
  u64 result;
  u16 sq_head;
  u16 sq_id;
  u16 command_id;
  u16 status;
};

/* Response capsule PDU: header + 16-byte completion queue entry */
struct ntp_tcp_rsp_pdu {
  struct ntp_tcp_hdr hdr;
  u64 result;
  u16 sq_head;
  u16 sq_id;
  u16 command_id;
  u16 status;
};

#endif /* NTPROF_KERNEL_TYPES_H */
