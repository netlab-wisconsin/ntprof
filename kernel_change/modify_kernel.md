# Modify the kernel source code

## Create nvme_tcp.h
Create a new file [`nvme_tcp.h`](kernel_change/include/trace/events/nvme_tcp.h) in the `include/trace/events` directory. This file defines the tracepoints for the nvme-tcp module.

## Create nvmet_tcp.h
Create a new file [`nvmet_tcp.h`](kernel_change/drivers/nvme/host/nvme_tcp.c) in the `include/trace/events` directory. This file defines the tracepoints for the nvmet-tcp module.

## Modify block/blk-core.c
Modify the `block/blk-core.c` file as folows. (Search the unchanged lines as key words in the file to locate the position)
- Export the predefined tracepoints `block_rq_complete`. 
  ``` diff
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_remap);
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_remap);
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_bio_complete);
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_split);
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_unplug);
    EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_insert);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(block_rq_complete);
  ```

## Modify drivers/nvme/host/tcp.c

Modify the `drivers/nvme/host/tcp.c` file as follows.

- Export the tracepoints.
  ``` diff
    #include "nvme.h"
    #include "fabrics.h"

  + #include "trace.h"
  + #define CREATE_TRACE_POINTS
  + #include <trace/events/nvme_tcp.h>
  + 
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_queue_rq);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_queue_request);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_try_send_cmd_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_try_send_data_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_try_send_data);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_done_send_req);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_handle_c2h_data);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_recv_data);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_process_nvme_cqe);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_handle_r2t);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_tcp_try_send);

    struct nvme_tcp_queue;

  /* Define the socket priority to use for connections were it 
  ```
- Add an attribute `recv_time` to `struct nvme-tcp_queue` to record network packet receive time.
  ``` diff
    struct nvme_tcp_queue {
      struct socket		*sock;
  +   u64 recv_time;
      struct work_struct	io_work;
      int			io_cpu;
  ```

- Enable recording timestamp in socket
  ``` diff
      nvme_tcp_reclassify_socket(queue->sock);

      /* Single syn retry */
      tcp_sock_set_syncnt(queue->sock->sk, 1);

      /* Set TCP no delay */
      tcp_sock_set_nodelay(queue->sock->sk);

  +   sock_enable_timestamps(queue->sock->sk);
  ```

- Inserts the tracepoints in functions

  ``` diff
    static inline void nvme_tcp_queue_request(struct nvme_tcp_request *req,
        bool sync, bool last)
    {
      struct nvme_tcp_queue *queue = req->queue;
      bool empty;

  +   trace_nvme_tcp_queue_request(blk_mq_rq_from_pdu(req), req->req.cmd, nvme_tcp_queue_id(queue));

      empty = llist_add(&req->lentry, &queue->req_list) &&
        list_empty(&queue->send_list) && !queue->request;

  ```

  ``` diff
    static int nvme_tcp_process_nvme_cqe(struct nvme_tcp_queue *queue,
        struct nvme_completion *cqe)
    {
      struct nvme_tcp_request *req;
      struct request *rq;

      rq = nvme_find_rq(nvme_tcp_tagset(queue), cqe->command_id);
      if (!rq) {
        dev_err(queue->ctrl->ctrl.device,
          "got bad cqe.command_id %#x on queue %d\n",
          cqe->command_id, nvme_tcp_queue_id(queue));
        nvme_tcp_error_recovery(&queue->ctrl->ctrl);
        return -EINVAL;
      }

  + 	trace_nvme_tcp_process_nvme_cqe(rq, (void *)queue->pdu, nvme_tcp_queue_id(queue), queue->recv_time);

      req = blk_mq_rq_to_pdu(rq);
      if (req->status == cpu_to_le16(NVME_SC_SUCCESS))
        req->status = cqe->status;

      if (!nvme_try_complete_req(rq, req->status, cqe->result))
        nvme_complete_rq(rq);
      queue->nr_cqe++;

      return 0;
    }
  ```

  ``` diff
    static int nvme_tcp_handle_c2h_data(struct nvme_tcp_queue *queue,
        struct nvme_tcp_data_pdu *pdu)
    {
      struct request *rq;

      rq = nvme_find_rq(nvme_tcp_tagset(queue), pdu->command_id);
      if (!rq) {
        dev_err(queue->ctrl->ctrl.device,
          "got bad c2hdata.command_id %#x on queue %d\n",
          pdu->command_id, nvme_tcp_queue_id(queue));
        return -ENOENT;
      }

      if (!blk_rq_payload_bytes(rq)) {
        dev_err(queue->ctrl->ctrl.device,
          "queue %d tag %#x unexpected data\n",
          nvme_tcp_queue_id(queue), rq->tag);
        return -EIO;
      }

      queue->data_remaining = le32_to_cpu(pdu->data_length);
  +   trace_nvme_tcp_handle_c2h_data(rq, nvme_tcp_queue_id(queue), queue->data_remaining, queue->recv_time);

      if (pdu->hdr.flags & NVME_TCP_F_DATA_SUCCESS &&
          unlikely(!(pdu->hdr.flags & NVME_TCP_F_DATA_LAST))) {
        dev_err(queue->ctrl->ctrl.device,
          "queue %d tag %#x SUCCESS set but not last PDU\n",
          nvme_tcp_queue_id(queue), rq->tag);
        nvme_tcp_error_recovery(&queue->ctrl->ctrl);
        return -EPROTO;
      }

      return 0;
    }
  ```

  ``` diff
    static int nvme_tcp_handle_r2t(struct nvme_tcp_queue *queue,
        struct nvme_tcp_r2t_pdu *pdu)
    {
      struct nvme_tcp_request *req;
      struct request *rq;
      int ret;

      rq = nvme_find_rq(nvme_tcp_tagset(queue), pdu->command_id);
      if (!rq) {
        dev_err(queue->ctrl->ctrl.device,
          "got bad r2t.command_id %#x on queue %d\n",
          pdu->command_id, nvme_tcp_queue_id(queue));
        return -ENOENT;
      }
  +   trace_nvme_tcp_handle_r2t(rq, pdu, nvme_tcp_queue_id(queue), queue->recv_time);
      req = blk_mq_rq_to_pdu(rq);

      ret = nvme_tcp_setup_h2c_data_pdu(req, pdu);
      if (unlikely(ret))
        return ret;

      req->state = NVME_TCP_SEND_H2C_PDU;
      req->offset = 0;

      nvme_tcp_queue_request(req, false, true);

      return 0;
    }
  ```

  ``` diff
    static int nvme_tcp_recv_skb(read_descriptor_t *desc, struct sk_buff *skb,
              unsigned int offset, size_t len)
    {
      struct nvme_tcp_queue *queue = desc->arg.data;
      size_t consumed = len;
      int result;

  +   queue->recv_time = skb->tstamp;

      while (len) {
        switch (nvme_tcp_recv_state(queue)) {
        case NVME_TCP_RECV_PDU:
          result = nvme_tcp_recv_pdu(queue, skb, &offset, &len);
          break;
        case NVME_TCP_RECV_DATA:
          result = nvme_tcp_recv_data(queue, skb, &offset, &len);
          break;
        case NVME_TCP_RECV_DDGST:
          result = nvme_tcp_recv_ddgst(queue, skb, &offset, &len);
          break;
        default:
          result = -EFAULT;
        }
        if (result) {
          dev_err(queue->ctrl->ctrl.device,
            "receive failed:  %d\n", result);
          queue->rd_enabled = false;
          nvme_tcp_error_recovery(&queue->ctrl->ctrl);
          return result;
        }
      }

      return consumed;
    }
  ```

  ``` diff
    static inline void nvme_tcp_done_send_req(struct nvme_tcp_queue *queue)
    {
  +   trace_nvme_tcp_done_send_req(blk_mq_rq_from_pdu(queue->request), nvme_tcp_queue_id(queue));
      queue->request = NULL;
    }
  ```

  ``` diff
    static int nvme_tcp_try_send_data(struct nvme_tcp_request *req)
    {
      struct nvme_tcp_queue *queue = req->queue;
      int req_data_len = req->data_len;

      while (true) {
        struct page *page = nvme_tcp_req_cur_page(req);
        size_t offset = nvme_tcp_req_cur_offset(req);
        size_t len = nvme_tcp_req_cur_length(req);
        bool last = nvme_tcp_pdu_last_send(req, len);
        int req_data_sent = req->data_sent;
        int ret, flags = MSG_DONTWAIT;

        if (last && !queue->data_digest && !nvme_tcp_queue_more(queue))
          flags |= MSG_EOR;
        else
          flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;

        if (sendpage_ok(page)) {
          ret = kernel_sendpage(queue->sock, page, offset, len,
              flags);
        } else {
          ret = sock_no_sendpage(queue->sock, page, offset, len,
              flags);
        }
        if (ret <= 0)
          return ret;

  +     trace_nvme_tcp_try_send_data(blk_mq_rq_from_pdu(req), req->pdu, nvme_tcp_queue_id(queue));

        if (queue->data_digest)
          nvme_tcp_ddgst_update(queue->snd_hash, page,
              offset, ret);

        /*
        * update the request iterator except for the last payload send
        * in the request where we don't want to modify it as we may
        * compete with the RX path completing the request.
        */
        if (req_data_sent + ret < req_data_len)
          nvme_tcp_advance_req(req, ret);

        /* fully successful last send in current PDU */
        if (last && ret == len) {
          if (queue->data_digest) {
            nvme_tcp_ddgst_final(queue->snd_hash,
              &req->ddgst);
            req->state = NVME_TCP_SEND_DDGST;
            req->offset = 0;
          } else {
            nvme_tcp_done_send_req(queue);
          }
          return 1;
        }
      }
      return -EAGAIN;
    }
  ```

  ``` diff
    static int nvme_tcp_try_send_cmd_pdu(struct nvme_tcp_request *req)
    {
      struct nvme_tcp_queue *queue = req->queue;
      struct nvme_tcp_cmd_pdu *pdu = req->pdu;
      bool inline_data = nvme_tcp_has_inline_data(req);
      u8 hdgst = nvme_tcp_hdgst_len(queue);
      int len = sizeof(*pdu) + hdgst - req->offset;
      int flags = MSG_DONTWAIT;
      int ret;

      if (inline_data || nvme_tcp_queue_more(queue))
        flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;
      else
        flags |= MSG_EOR;

      if (queue->hdr_digest && !req->offset)
        nvme_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
      
  +   trace_nvme_tcp_try_send_cmd_pdu(blk_mq_rq_from_pdu(req), queue->sock, nvme_tcp_queue_id(queue), 0);
      ret = kernel_sendpage(queue->sock, virt_to_page(pdu),
          offset_in_page(pdu) + req->offset, len,  flags);
      
      if (unlikely(ret <= 0))
        return ret;

      len -= ret;
      if (!len) {
        if (inline_data) {
          req->state = NVME_TCP_SEND_DATA;
          if (queue->data_digest)
            crypto_ahash_init(queue->snd_hash);
        } else {
          nvme_tcp_done_send_req(queue);
        }
        return 1;
      }
      req->offset += ret;

      return -EAGAIN;
    }
  ```

  ``` diff
    static int nvme_tcp_try_send_data_pdu(struct nvme_tcp_request *req)
    {
      struct nvme_tcp_queue *queue = req->queue;
      struct nvme_tcp_data_pdu *pdu = req->pdu;
      u8 hdgst = nvme_tcp_hdgst_len(queue);
      int len = sizeof(*pdu) - req->offset + hdgst;
      int ret;

      if (queue->hdr_digest && !req->offset)
        nvme_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));

  +   trace_nvme_tcp_try_send_data_pdu(blk_mq_rq_from_pdu(req), pdu, nvme_tcp_queue_id(queue));

      ret = kernel_sendpage(queue->sock, virt_to_page(pdu),
          offset_in_page(pdu) + req->offset, len,
          MSG_DONTWAIT | MSG_MORE | MSG_SENDPAGE_NOTLAST);
      if (unlikely(ret <= 0))
        return ret;

      len -= ret;
      if (!len) {
        req->state = NVME_TCP_SEND_DATA;
        if (queue->data_digest)
          crypto_ahash_init(queue->snd_hash);
        return 1;
      }
      req->offset += ret;

      return -EAGAIN;
    }
  ```

  ``` diff
    static int nvme_tcp_try_send(struct nvme_tcp_queue *queue)
    {
      struct nvme_tcp_request *req;
      int ret = 1;

      if (!queue->request) {
        queue->request = nvme_tcp_fetch_request(queue);
        if (!queue->request)
          return 0;
      }
      req = queue->request;

  +   trace_nvme_tcp_try_send(blk_mq_rq_from_pdu(req), nvme_tcp_queue_id(queue));

      if (req->state == NVME_TCP_SEND_CMD_PDU) {
        ret = nvme_tcp_try_send_cmd_pdu(req);
        if (ret <= 0)
          goto done;
        if (!nvme_tcp_has_inline_data(req))
          return ret;
      }

      if (req->state == NVME_TCP_SEND_H2C_PDU) {
        ret = nvme_tcp_try_send_data_pdu(req);
        if (ret <= 0)
          goto done;
      }

      if (req->state == NVME_TCP_SEND_DATA) {
        ret = nvme_tcp_try_send_data(req);
        if (ret <= 0)
          goto done;
      }

      if (req->state == NVME_TCP_SEND_DDGST)
        ret = nvme_tcp_try_send_ddgst(req);
    done:
      if (ret == -EAGAIN) {
        ret = 0;
      } else if (ret < 0) {
        dev_err(queue->ctrl->ctrl.device,
          "failed to send request %d\n", ret);
        nvme_tcp_fail_request(queue->request);
        nvme_tcp_done_send_req(queue);
      }
      return ret;
    }
  ```

  ``` diff
    static blk_status_t nvme_tcp_queue_rq(struct blk_mq_hw_ctx *hctx,
        const struct blk_mq_queue_data *bd)
    {
      struct nvme_ns *ns = hctx->queue->queuedata;
      struct nvme_tcp_queue *queue = hctx->driver_data;
      struct request *rq = bd->rq;
      struct nvme_tcp_request *req = blk_mq_rq_to_pdu(rq);
      bool queue_ready = test_bit(NVME_TCP_Q_LIVE, &queue->flags);
      blk_status_t ret;


      if (!nvme_check_ready(&queue->ctrl->ctrl, rq, queue_ready))
        return nvme_fail_nonready_command(&queue->ctrl->ctrl, rq);

      ret = nvme_tcp_setup_cmd_pdu(ns, rq);
      if (unlikely(ret))
        return ret;

      blk_mq_start_request(rq);
      
  +   trace_nvme_tcp_queue_rq(rq, req->pdu, nvme_tcp_queue_id(queue), &queue->req_list, &queue->send_list, &queue->send_mutex);

      nvme_tcp_queue_request(req, true, bd->last);

      return BLK_STS_OK;
    }
  ```

## Modify drivers/nvme/target/tcp.c

Modify the `drivers/nvme/target/tcp.c` file as follows.

- Export the tracepoint symbols.
  ``` diff
    #include "nvmet.h"

  + #define CREATE_TRACE_POINTS
  + #include <trace/events/nvmet_tcp.h>
  + #include <linux/tracepoint.h>
  +
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_done_recv_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_exec_read_req);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_exec_write_req);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_queue_response);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_setup_c2h_data_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_setup_r2t_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_setup_response_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_try_send_data_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_try_send_r2t);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_try_send_response);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_try_send_data);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_handle_h2c_data_pdu);
  + EXPORT_TRACEPOINT_SYMBOL_GPL(nvmet_tcp_try_recv_data);
  +
    #define NVMET_TCP_DEF_INLINE_DATA_SIZE	(4 * PAGE_SIZE)
    #define NVMET_TCP_MAXH2CDATA		0x400000 /* 16M arbitrary limit */
  ```

- Add an attribute `recv_time` to `struct nvmet_tcp_queue` to record network packet receive time.
  ``` diff
    struct nvmet_tcp_queue {
      struct socket		*sock;
  +   s64 recv_time;
      struct nvmet_tcp_port	*port;
      struct work_struct	io_work;
  ```

- Enable recording timestamp in socket
  ``` diff
      mutex_lock(&nvmet_tcp_queue_mutex);
      list_add_tail(&queue->queue_list, &nvmet_tcp_queue_list);
      mutex_unlock(&nvmet_tcp_queue_mutex);

      ret = nvmet_tcp_set_queue_sock(queue);
      if (ret)
        goto out_destroy_sq;

  +   sock_enable_timestamps(queue->sock->sk);

      return 0;
  ```


- Insert the tracepoints in the following functions.

  ``` diff
    static void nvmet_setup_c2h_data_pdu(struct nvmet_tcp_cmd *cmd)
    {
      struct nvme_tcp_data_pdu *pdu = cmd->data_pdu;
      struct nvmet_tcp_queue *queue = cmd->queue;
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
      u8 ddgst = nvmet_tcp_ddgst_len(cmd->queue);

  +   trace_nvmet_tcp_setup_c2h_data_pdu(cmd->req.cqe, queue->idx);

      cmd->offset = 0;
      cmd->state = NVMET_TCP_SEND_DATA_PDU;

      pdu->hdr.type = nvme_tcp_c2h_data;
      pdu->hdr.flags = NVME_TCP_F_DATA_LAST | (queue->nvme_sq.sqhd_disabled ?
                NVME_TCP_F_DATA_SUCCESS : 0);
      pdu->hdr.hlen = sizeof(*pdu);
      pdu->hdr.pdo = pdu->hdr.hlen + hdgst;
      pdu->hdr.plen =
        cpu_to_le32(pdu->hdr.hlen + hdgst +
            cmd->req.transfer_len + ddgst);
      pdu->command_id = cmd->req.cqe->command_id;
      pdu->data_length = cpu_to_le32(cmd->req.transfer_len);
      pdu->data_offset = cpu_to_le32(cmd->wbytes_done);

      if (queue->data_digest) {
        pdu->hdr.flags |= NVME_TCP_F_DDGST;
        nvmet_tcp_send_ddgst(queue->snd_hash, cmd);
      }

      if (cmd->queue->hdr_digest) {
        pdu->hdr.flags |= NVME_TCP_F_HDGST;
        nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
      }
    }
  ```

  ``` diff 
    static void nvmet_setup_r2t_pdu(struct nvmet_tcp_cmd *cmd)
    {
      struct nvme_tcp_r2t_pdu *pdu = cmd->r2t_pdu;
      struct nvmet_tcp_queue *queue = cmd->queue;
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);

  +   trace_nvmet_tcp_setup_r2t_pdu(cmd->req.cmd, queue->idx);

      cmd->offset = 0;
      cmd->state = NVMET_TCP_SEND_R2T;

      pdu->hdr.type = nvme_tcp_r2t;
      pdu->hdr.flags = 0;
      pdu->hdr.hlen = sizeof(*pdu);
      pdu->hdr.pdo = 0;
      pdu->hdr.plen = cpu_to_le32(pdu->hdr.hlen + hdgst);

      pdu->command_id = cmd->req.cmd->common.command_id;
      pdu->ttag = nvmet_tcp_cmd_tag(cmd->queue, cmd);
      pdu->r2t_length = cpu_to_le32(cmd->req.transfer_len - cmd->rbytes_done);
      pdu->r2t_offset = cpu_to_le32(cmd->rbytes_done);
      if (cmd->queue->hdr_digest) {
        pdu->hdr.flags |= NVME_TCP_F_HDGST;
        nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
      }
    }
  ```

  ``` diff
    static void nvmet_setup_response_pdu(struct nvmet_tcp_cmd *cmd)
    {
      struct nvme_tcp_rsp_pdu *pdu = cmd->rsp_pdu;
      struct nvmet_tcp_queue *queue = cmd->queue;
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);

  +   trace_nvmet_tcp_setup_response_pdu(cmd->req.cqe, queue->idx);

      cmd->offset = 0;
      cmd->state = NVMET_TCP_SEND_RESPONSE;

      pdu->hdr.type = nvme_tcp_rsp;
      pdu->hdr.flags = 0;
      pdu->hdr.hlen = sizeof(*pdu);
      pdu->hdr.pdo = 0;
      pdu->hdr.plen = cpu_to_le32(pdu->hdr.hlen + hdgst);
      if (cmd->queue->hdr_digest) {
        pdu->hdr.flags |= NVME_TCP_F_HDGST;
        nvmet_tcp_hdgst(queue->snd_hash, pdu, sizeof(*pdu));
      }
    }
  ```

  ``` diff
    static void nvmet_tcp_queue_response(struct nvmet_req *req)
    {
      struct nvmet_tcp_cmd *cmd =
        container_of(req, struct nvmet_tcp_cmd, req);
      struct nvmet_tcp_queue	*queue = cmd->queue;
      struct nvme_sgl_desc *sgl;
      u32 len;

      if (unlikely(cmd == queue->cmd)) {
        sgl = &cmd->req.cmd->common.dptr.sgl;
        len = le32_to_cpu(sgl->length);

        /*
        * Wait for inline data before processing the response.
        * Avoid using helpers, this might happen before
        * nvmet_req_init is completed.
        */
        if (queue->rcv_state == NVMET_TCP_RECV_PDU &&
            len && len <= cmd->req.port->inline_data_size &&
            nvme_is_write(cmd->req.cmd))
          return;
      }

  +   trace_nvmet_tcp_queue_response(cmd->req.cmd, queue->idx);

      llist_add(&cmd->lentry, &queue->resp_list);
      queue_work_on(queue_cpu(queue), nvmet_tcp_wq, &cmd->queue->io_work);
    }
  ```

  ``` diff 
    static void nvmet_tcp_execute_request(struct nvmet_tcp_cmd *cmd)
    {
      if (unlikely(cmd->flags & NVMET_TCP_F_INIT_FAILED))
        nvmet_tcp_queue_response(&cmd->req);
  -   else
  +   else {
  +     int size = nvme_is_write(cmd->req.cmd) ?
  +       cmd->req.transfer_len : 0;
  +     trace_nvmet_tcp_exec_write_req(cmd->req.cmd, cmd->queue->idx, size);
        cmd->req.execute(&cmd->req);
  +   }
    }
  ```

  ``` diff
    static int nvmet_try_send_data_pdu(struct nvmet_tcp_cmd *cmd)
    {
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
      int left = sizeof(*cmd->data_pdu) - cmd->offset + hdgst;
      int ret;

  +   trace_nvmet_tcp_try_send_data_pdu(cmd->req.cqe, cmd->data_pdu, cmd->queue->idx, left);

      ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->data_pdu),
          offset_in_page(cmd->data_pdu) + cmd->offset,
          left, MSG_DONTWAIT | MSG_MORE | MSG_SENDPAGE_NOTLAST);
      if (ret <= 0)
        return ret;

      cmd->offset += ret;
      left -= ret;

      if (left)
        return -EAGAIN;

      cmd->state = NVMET_TCP_SEND_DATA;
      cmd->offset  = 0;
      return 1;
    }
  ```

  ``` diff
    static int nvmet_try_send_data(struct nvmet_tcp_cmd *cmd, bool last_in_batch)
    {
      struct nvmet_tcp_queue *queue = cmd->queue;
      int ret;

      while (cmd->cur_sg) {
        struct page *page = sg_page(cmd->cur_sg);
        u32 left = cmd->cur_sg->length - cmd->offset;
        int flags = MSG_DONTWAIT;

        if ((!last_in_batch && cmd->queue->send_list_len) ||
            cmd->wbytes_done + left < cmd->req.transfer_len ||
            queue->data_digest || !queue->nvme_sq.sqhd_disabled)
          flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;

        ret = kernel_sendpage(cmd->queue->sock, page, cmd->offset,
              left, flags);
        if (ret <= 0)
          return ret;
        
  +     trace_nvmet_tcp_try_send_data(cmd->req.cqe, cmd->queue->idx, ret);

        cmd->offset += ret;
        cmd->wbytes_done += ret;

        /* Done with sg?*/
        if (cmd->offset == cmd->cur_sg->length) {
          cmd->cur_sg = sg_next(cmd->cur_sg);
          cmd->offset = 0;
        }
      }

      if (queue->data_digest) {
        cmd->state = NVMET_TCP_SEND_DDGST;
        cmd->offset = 0;
      } else {
        if (queue->nvme_sq.sqhd_disabled) {
          cmd->queue->snd_cmd = NULL;
          nvmet_tcp_put_cmd(cmd);
        } else {
          nvmet_setup_response_pdu(cmd);
        }
      }

      if (queue->nvme_sq.sqhd_disabled) {
        kfree(cmd->iov);
        sgl_free(cmd->req.sg);
      }

      return 1;

    }
  ```

  ``` diff
    static int nvmet_try_send_response(struct nvmet_tcp_cmd *cmd,
        bool last_in_batch)
    {
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
      int left = sizeof(*cmd->rsp_pdu) - cmd->offset + hdgst;
      int flags = MSG_DONTWAIT;
      int ret;

      if (!last_in_batch && cmd->queue->send_list_len)
        flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;
      else
        flags |= MSG_EOR;

  +   trace_nvmet_tcp_try_send_response(cmd->req.cqe, cmd->rsp_pdu, cmd->queue->idx, left);

      ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->rsp_pdu),
        offset_in_page(cmd->rsp_pdu) + cmd->offset, left, flags);
      if (ret <= 0)
        return ret;
      cmd->offset += ret;
      left -= ret;

      if (left)
        return -EAGAIN;

      kfree(cmd->iov);
      sgl_free(cmd->req.sg);
      cmd->queue->snd_cmd = NULL;
      nvmet_tcp_put_cmd(cmd);
      return 1;
    }
  ```

  ``` diff
    static int nvmet_try_send_r2t(struct nvmet_tcp_cmd *cmd, bool last_in_batch)
    {
      u8 hdgst = nvmet_tcp_hdgst_len(cmd->queue);
      int left = sizeof(*cmd->r2t_pdu) - cmd->offset + hdgst;
      int flags = MSG_DONTWAIT;
      int ret;

      if (!last_in_batch && cmd->queue->send_list_len)
        flags |= MSG_MORE | MSG_SENDPAGE_NOTLAST;
      else
        flags |= MSG_EOR;

  +   trace_nvmet_tcp_try_send_r2t(cmd->req.cmd, cmd->r2t_pdu, cmd->queue->idx, left);

      ret = kernel_sendpage(cmd->queue->sock, virt_to_page(cmd->r2t_pdu),
        offset_in_page(cmd->r2t_pdu) + cmd->offset, left, flags);
      if (ret <= 0)
        return ret;
      cmd->offset += ret;
      left -= ret;

      if (left)
        return -EAGAIN;

      cmd->queue->snd_cmd = NULL;
      return 1;
    }
  ```

  ``` diff
    static int nvmet_tcp_handle_h2c_data_pdu(struct nvmet_tcp_queue *queue)
    {
      struct nvme_tcp_data_pdu *data = &queue->pdu.data;
      struct nvmet_tcp_cmd *cmd;
      unsigned int exp_data_len;

      if (likely(queue->nr_cmds)) {
        if (unlikely(data->ttag >= queue->nr_cmds)) {
          pr_err("queue %d: received out of bound ttag %u, nr_cmds %u\n",
            queue->idx, data->ttag, queue->nr_cmds);
          nvmet_tcp_fatal_error(queue);
          return -EPROTO;
        }
        cmd = &queue->cmds[data->ttag];
      } else {
        cmd = &queue->connect;
      }

      if (le32_to_cpu(data->data_offset) != cmd->rbytes_done) {
        pr_err("ttag %u unexpected data offset %u (expected %u)\n",
          data->ttag, le32_to_cpu(data->data_offset),
          cmd->rbytes_done);
        /* FIXME: use path and transport errors */
        nvmet_tcp_fatal_error(queue);
        return -EPROTO;
      }

      exp_data_len = le32_to_cpu(data->hdr.plen) -
          nvmet_tcp_hdgst_len(queue) -
          nvmet_tcp_ddgst_len(queue) -
          sizeof(*data);

  +   trace_nvmet_tcp_handle_h2c_data_pdu(data, cmd->req.cmd, queue->idx, exp_data_len, queue->recv_time);

      cmd->pdu_len = le32_to_cpu(data->data_length);
      if (unlikely(cmd->pdu_len != exp_data_len ||
            cmd->pdu_len == 0 ||
            cmd->pdu_len > NVMET_TCP_MAXH2CDATA)) {
        pr_err("H2CData PDU len %u is invalid\n", cmd->pdu_len);
        /* FIXME: use proper transport errors */
        nvmet_tcp_fatal_error(queue);
        return -EPROTO;
      }
      cmd->pdu_recv = 0;
      nvmet_tcp_map_pdu_iovec(cmd);
      queue->cmd = cmd;
      queue->rcv_state = NVMET_TCP_RECV_DATA;

      return 0;
    }
  ```

  ``` diff
    static int nvmet_tcp_done_recv_pdu(struct nvmet_tcp_queue *queue)
    {
      struct nvme_tcp_hdr *hdr = &queue->pdu.cmd.hdr;
      struct nvme_command *nvme_cmd = &queue->pdu.cmd.cmd;
      struct nvmet_req *req;
      int ret;

      if (unlikely(queue->state == NVMET_TCP_Q_CONNECTING)) {
        if (hdr->type != nvme_tcp_icreq) {
          pr_err("unexpected pdu type (%d) before icreq\n",
            hdr->type);
          nvmet_tcp_fatal_error(queue);
          return -EPROTO;
        }
        return nvmet_tcp_handle_icreq(queue);
      }

      if (hdr->type == nvme_tcp_h2c_data) {
        ret = nvmet_tcp_handle_h2c_data_pdu(queue);
        if (unlikely(ret))
          return ret;
        return 0;
      }

      queue->cmd = nvmet_tcp_get_cmd(queue);
      if (unlikely(!queue->cmd)) {
        /* This should never happen */
        pr_err("queue %d: out of commands (%d) send_list_len: %d, opcode: %d",
          queue->idx, queue->nr_cmds, queue->send_list_len,
          nvme_cmd->common.opcode);
        nvmet_tcp_fatal_error(queue);
        return -ENOMEM;
      }

      req = &queue->cmd->req;
      memcpy(req->cmd, nvme_cmd, sizeof(*nvme_cmd));

  +   trace_nvmet_tcp_done_recv_pdu(&queue->pdu.cmd, queue->idx, queue->recv_time);

      if (unlikely(!nvmet_req_init(req, &queue->nvme_cq,
          &queue->nvme_sq, &nvmet_tcp_ops))) {
        pr_err("failed cmd %p id %d opcode %d, data_len: %d\n",
          req->cmd, req->cmd->common.command_id,
          req->cmd->common.opcode,
          le32_to_cpu(req->cmd->common.dptr.sgl.length));

        nvmet_tcp_handle_req_failure(queue, queue->cmd, req);
        return 0;
      }

      ret = nvmet_tcp_map_data(queue->cmd);
      if (unlikely(ret)) {
        pr_err("queue %d: failed to map data\n", queue->idx);
        if (nvmet_tcp_has_inline_data(queue->cmd))
          nvmet_tcp_fatal_error(queue);
        else
          nvmet_req_complete(req, ret);
        ret = -EAGAIN;
        goto out;
      }

      if (nvmet_tcp_need_data_in(queue->cmd)) {
        if (nvmet_tcp_has_inline_data(queue->cmd)) {
          queue->rcv_state = NVMET_TCP_RECV_DATA;
          nvmet_tcp_map_pdu_iovec(queue->cmd);
          return 0;
        }
        /* send back R2T */
        nvmet_tcp_queue_response(&queue->cmd->req);
        goto out;
      }

      trace_nvmet_tcp_exec_read_req(req->cmd, queue->idx);

      queue->cmd->req.execute(&queue->cmd->req);
    out:
      nvmet_prepare_receive_pdu(queue);
      return ret;
    }
  ```

  ``` diff
    static int nvmet_tcp_done_recv_pdu(struct nvmet_tcp_queue *queue)
    {
      struct nvme_tcp_hdr *hdr = &queue->pdu.cmd.hdr;
      struct nvme_command *nvme_cmd = &queue->pdu.cmd.cmd;
      struct nvmet_req *req;
      int ret;

      if (unlikely(queue->state == NVMET_TCP_Q_CONNECTING)) {
        if (hdr->type != nvme_tcp_icreq) {
          pr_err("unexpected pdu type (%d) before icreq\n",
            hdr->type);
          nvmet_tcp_fatal_error(queue);
          return -EPROTO;
        }
        return nvmet_tcp_handle_icreq(queue);
      }

      if (hdr->type == nvme_tcp_h2c_data) {
        ret = nvmet_tcp_handle_h2c_data_pdu(queue);
        if (unlikely(ret))
          return ret;
        return 0;
      }

      queue->cmd = nvmet_tcp_get_cmd(queue);
      if (unlikely(!queue->cmd)) {
        /* This should never happen */
        pr_err("queue %d: out of commands (%d) send_list_len: %d, opcode: %d",
          queue->idx, queue->nr_cmds, queue->send_list_len,
          nvme_cmd->common.opcode);
        nvmet_tcp_fatal_error(queue);
        return -ENOMEM;
      }

      req = &queue->cmd->req;
      memcpy(req->cmd, nvme_cmd, sizeof(*nvme_cmd));

  +   trace_nvmet_tcp_done_recv_pdu(&queue->pdu.cmd, queue->idx, queue->recv_time);

      if (unlikely(!nvmet_req_init(req, &queue->nvme_cq,
          &queue->nvme_sq, &nvmet_tcp_ops))) {
        pr_err("failed cmd %p id %d opcode %d, data_len: %d\n",
          req->cmd, req->cmd->common.command_id,
          req->cmd->common.opcode,
          le32_to_cpu(req->cmd->common.dptr.sgl.length));

        nvmet_tcp_handle_req_failure(queue, queue->cmd, req);
        return 0;
      }

      ret = nvmet_tcp_map_data(queue->cmd);
      if (unlikely(ret)) {
        pr_err("queue %d: failed to map data\n", queue->idx);
        if (nvmet_tcp_has_inline_data(queue->cmd))
          nvmet_tcp_fatal_error(queue);
        else
          nvmet_req_complete(req, ret);
        ret = -EAGAIN;
        goto out;
      }

      if (nvmet_tcp_need_data_in(queue->cmd)) {
        if (nvmet_tcp_has_inline_data(queue->cmd)) {
          queue->rcv_state = NVMET_TCP_RECV_DATA;
          nvmet_tcp_map_pdu_iovec(queue->cmd);
          return 0;
        }
        /* send back R2T */
        nvmet_tcp_queue_response(&queue->cmd->req);
        goto out;
      }

      trace_nvmet_tcp_exec_read_req(req->cmd, queue->idx);

      queue->cmd->req.execute(&queue->cmd->req);
    out:
      nvmet_prepare_receive_pdu(queue);
      return ret;
    }
  ```

  ``` diff
    static int nvmet_tcp_try_recv_pdu(struct nvmet_tcp_queue *queue)
    {
      struct nvme_tcp_hdr *hdr = &queue->pdu.cmd.hdr;
      int len;
      struct kvec iov;
      struct msghdr msg = { .msg_flags = MSG_DONTWAIT };
  +   queue->recv_time = 0;

    recv:
  +   char ckbuf[CMSG_SPACE(sizeof(struct __kernel_old_timespec))];
  +   msg.msg_control = ckbuf;
  +   msg.msg_controllen = sizeof(ckbuf);
  +   struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); 

      iov.iov_base = (void *)&queue->pdu + queue->offset;
      iov.iov_len = queue->left;
      len = kernel_recvmsg(queue->sock, &msg, &iov, 1,
          iov.iov_len, msg.msg_flags);
      if (unlikely(len < 0))
        return len;

  +   if(cmsg && queue->recv_time == 0){
  +     struct __kernel_old_timespec * ts = (struct __kernel_old_timespec *)CMSG_DATA(cmsg);
  +     queue->recv_time = ((s64) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
  +   }

      queue->offset += len;
      queue->left -= len;
      if (queue->left)
        return -EAGAIN;

      if (queue->offset == sizeof(struct nvme_tcp_hdr)) {
        u8 hdgst = nvmet_tcp_hdgst_len(queue);

        if (unlikely(!nvmet_tcp_pdu_valid(hdr->type))) {
          pr_err("unexpected pdu type %d\n", hdr->type);
          nvmet_tcp_fatal_error(queue);
          return -EIO;
        }

        if (unlikely(hdr->hlen != nvmet_tcp_pdu_size(hdr->type))) {
          pr_err("pdu %d bad hlen %d\n", hdr->type, hdr->hlen);
          return -EIO;
        }

        queue->left = hdr->hlen - queue->offset + hdgst;
        goto recv;
      }

      if (queue->hdr_digest &&
          nvmet_tcp_verify_hdgst(queue, &queue->pdu, hdr->hlen)) {
        nvmet_tcp_fatal_error(queue); /* fatal */
        return -EPROTO;
      }

      if (queue->data_digest &&
          nvmet_tcp_check_ddgst(queue, &queue->pdu)) {
        nvmet_tcp_fatal_error(queue); /* fatal */
        return -EPROTO;
      }

      return nvmet_tcp_done_recv_pdu(queue);
    }
  ```

  ``` diff
    static int nvmet_tcp_try_recv_data(struct nvmet_tcp_queue *queue)
    {
      struct nvmet_tcp_cmd  *cmd = queue->cmd;
      int ret;

      while (msg_data_left(&cmd->recv_msg)) {
  +     char ckbuf[CMSG_SPACE(sizeof(struct __kernel_old_timespec))];
  +     cmd->recv_msg.msg_control = ckbuf;
  +     cmd->recv_msg.msg_controllen = sizeof(ckbuf);
  +     struct cmsghdr *cmsg = CMSG_FIRSTHDR(&cmd->recv_msg); 

        ret = sock_recvmsg(cmd->queue->sock, &cmd->recv_msg,
          cmd->recv_msg.msg_flags);
        if (ret <= 0)
          return ret;

  +     if(cmsg && CMSG_DATA(cmsg)){
  +       struct __kernel_old_timespec * ts = (struct __kernel_old_timespec *)CMSG_DATA(cmsg);
  +       long long recv_time = ((s64) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
  +       trace_nvmet_tcp_try_recv_data(cmd->req.cmd, cmd->queue->idx, ret, recv_time);
  +     }

        cmd->pdu_recv += ret;
        cmd->rbytes_done += ret;
      }

      nvmet_tcp_unmap_pdu_iovec(cmd);
      if (queue->data_digest) {
        nvmet_tcp_prep_recv_ddgst(cmd);
        return 0;
      }

      if (cmd->rbytes_done == cmd->req.transfer_len)
        nvmet_tcp_execute_request(cmd);

      nvmet_prepare_receive_pdu(queue);
      return 0;
    }
  ```

# Modify include/linux/nvme-tcp.h

Modify the `include/linux/nvme-tcp.h` file as follows.

- Define a struct to store profiling data from target side.
  ``` diff
    struct nvme_tcp_hdr {
      __u8	type;
      __u8	flags;
      __u8	hlen;
      __u8	pdo;
      __le32	plen;
    };

  + #define MAX_NVME_TCP_EVENTS 10
  + struct ntprof_stat{
  +   __u64 id;            
  +   __u8 tag;            
  +   __u8 cnt;            
  +   __u8 event[MAX_NVME_TCP_EVENTS];      
  +   __u64 ts[MAX_NVME_TCP_EVENTS];        
  + };
  ```

- Add the `ntprof_stat` at the end of each type of PDU
  ``` diff
    struct nvme_tcp_icreq_pdu {
      struct nvme_tcp_hdr	hdr;
      __le16			pfv;
      __u8			hpda;
      __u8			digest;
      __le32			maxr2t;
      __u8			rsvd2[112];
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_icresp_pdu {
      struct nvme_tcp_hdr	hdr;
      __le16			pfv;
      __u8			cpda;
      __u8			digest;
      __le32			maxdata;
      __u8			rsvd[112];
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_term_pdu {
      struct nvme_tcp_hdr	hdr;
      __le16			fes;
      __le16			feil;
      __le16			feiu;
      __u8			rsvd[10];
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_cmd_pdu {
      struct nvme_tcp_hdr	hdr;
      struct nvme_command	cmd;
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_rsp_pdu {
      struct nvme_tcp_hdr	hdr;
      struct nvme_completion	cqe;
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_r2t_pdu {
      struct nvme_tcp_hdr	hdr;
      __u16			command_id;
      __u16			ttag;
      __le32			r2t_offset;
      __le32			r2t_length;
      __u8			rsvd[4];
  +   struct ntprof_stat stat;
    };
  ```

  ``` diff
    struct nvme_tcp_data_pdu {
      struct nvme_tcp_hdr	hdr;
      __u16			command_id;
      __u16			ttag;
      __le32			data_offset;
      __le32			data_length;
      __u8			rsvd[4];
  +   struct ntprof_stat stat;
    };
  ```