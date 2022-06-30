//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//

#include "rt_asp_i.h"

#define ASP_PORT 9700

#define ASP_TRX_ACK_TO_SEC 3
#define ASP_TRX_VER_EXCHG_TO_SEC 10

static void asp_trx_cmd_complete(struct asp_trx_cmd *cmd);
static void asp_trx_process_queued_cmd(void *eloop_data, void *user_ctx);
static void asp_trx_cmd_age_func(void *eloop_data, void *user_ctx);
static void asp_trx_cmd_resp_timeout(void *eloop_data, void *user_ctx);
static void asp_trx_rx(int sd, void *eloop_ctx, void *sock_ctx);
static void asp_trx_on_ver_exchg_complete(struct asp_trx_info *trx);
static void asp_trx_ver_exchg_timeout(void *eloop_data, void *user_ctx);

//------------------------------------------------------------------------------
//
// Local functions: socket
//
//------------------------------------------------------------------------------
static int asp_trx_init_sock(struct asp_trx_info *trx, struct in_addr local_ip)
{
	struct sockaddr_in addr;

	trx->sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (trx->sd < 0) {
		wpa_printf(MSG_ERROR, "socket(AF_INET): %s", strerror(errno));
		goto fail;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ASP_PORT);
	addr.sin_addr = local_ip;

	if (bind(trx->sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": bind(AF_INET): %s\n",
			   strerror(errno));
		goto fail;
	}

	return 0;

fail:
	if (0 < trx->sd)
		close (trx->sd);

	return -1;
}

static void asp_trx_deinit_sock(struct asp_trx_info *trx)
{
	if (trx->sd <= 0)
		return;

	close (trx->sd);
	trx->sd = -1;
}

static size_t asp_trx_sock_send(int sd, struct in_addr dest,
				const struct wpabuf *buf)
{
	struct sockaddr_in addr;
	size_t nsent;

	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr = dest;
	addr.sin_port = htons(ASP_PORT);

	nsent = sendto(sd, wpabuf_head(buf), wpabuf_len(buf), 0,
		       (struct sockaddr *) &addr, sizeof(addr));
	if (nsent < 0)
		wpa_printf(MSG_ERROR, ASP_TAG": send failed: "
			   "%d (%s)", errno, strerror(errno));

	return nsent;
}

static size_t asp_trx_sock_recv(int sd, struct wpabuf *buf)
{
	struct sockaddr_in addr; /* client address */
	socklen_t addr_len;
	int nread;

	addr_len = sizeof(addr);
	nread = recvfrom(sd, wpabuf_mhead_u8(buf),
			 wpabuf_size(buf), 0,
			 (struct sockaddr *) &addr, &addr_len);

	if (nread <= 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": recvfrom: %s\n",
			   strerror(errno));
	} else {
		wpabuf_put(buf, nread);
		wpa_hexdump_ascii(MSG_INFO, "rx-a:",
				  wpabuf_head_u8(buf), wpabuf_len(buf));
	}

	return nread;
}

static int asp_trx_sock_inited(const struct asp_trx_info *trx)
{
	if (!(trx->flag & ASP_TRX_FLAG_INITIALIZED))
		return 0;
	if (trx->flag & ASP_TRX_FLAG_DEINITIALIZED)
		return 0;

	return 1;
}

//------------------------------------------------------------------------------
//
// Local functions: cmd
//
//------------------------------------------------------------------------------
static int asp_trx_cmd_in_queue(struct asp_trx_info *trx,
				struct asp_trx_cmd *cmd)
{
	struct asp_trx_cmd *tmp;

	/* Find cmd in queue */
	dl_list_for_each(tmp, &trx->cmd_queue,
			 struct asp_trx_cmd, list) {
		if (tmp == cmd)
			break;
	}

	return (tmp == cmd) ? 1 : 0;
}

static int asp_trx_cancel_cur_cmd(struct asp_trx_info *trx)
{
	if (trx->cur_cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": No cur_cmd could be canceled\n");
		return -1;
	}

	wpa_printf(MSG_INFO, ASP_TAG": cur_cmd(%u) canceled, flag: 0x%08X\n",
		   trx->cur_cmd->seq, trx->cur_cmd->flag);

	eloop_cancel_timeout(asp_trx_cmd_resp_timeout,
			     trx->cur_cmd, ELOOP_ALL_CTX);
	eloop_cancel_timeout(asp_trx_cmd_age_func,
			     trx->cur_cmd, ELOOP_ALL_CTX);
	trx->cur_cmd = NULL;

	return 0;
}

static int asp_trx_cancel_queued_cmd(struct asp_trx_info *trx,
				     struct asp_trx_cmd *cmd)
{
	if (!asp_trx_cmd_in_queue(trx, cmd)) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Unable to cancel cmd(%u), \
not found, flag: 0x%08X\n",
			   cmd->seq, cmd->flag);
		return -1;
	}

	/* Cancel cmd in queue */
	dl_list_del(&cmd->list);
	eloop_cancel_timeout(asp_trx_cmd_age_func, cmd, ELOOP_ALL_CTX);

	return 0;
}

static void asp_trx_free_all_cmd(struct asp_trx_info *trx)
{
	struct asp_trx_cmd *cur, *cmd, *tmp;

	/* Cancel cur_cmd */
	if ((cur = trx->cur_cmd) != NULL) {
		asp_trx_cancel_cur_cmd(trx);
		asp_trx_free_cmd(cur);
	}

	/* Cancel queued cmds */
	dl_list_for_each_safe(cmd, tmp, &trx->cmd_queue, struct asp_trx_cmd,
			      list) {
		asp_trx_cancel_queued_cmd(trx, cmd);
		asp_trx_free_cmd(cmd);
	}
}

//------------------------------------------------------------------------------
//
// Local functions: tx
//
//------------------------------------------------------------------------------
static void asp_trx_process_queued_cmd(void *eloop_data, void *user_ctx)
{
	struct asp_trx_info *trx = (struct asp_trx_info *)eloop_data;
	struct asp_trx_cmd *cmd;

	if (trx->cur_cmd) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": processing queued cmd but cur_cmd(%u) \
is not NULL\n",
			trx->cur_cmd->seq);
		return;
	}

	/* TODO: check if session removed */

	/* Process the 1st in the queue */
	cmd = (struct asp_trx_cmd *)dl_list_first(&trx->cmd_queue,
						  struct asp_trx_cmd, list);
	if (cmd == NULL)
		return;

	dl_list_del(&cmd->list);

	/* Cancel age timeout */
	eloop_cancel_timeout(asp_trx_cmd_age_func, cmd, ELOOP_ALL_CTX);

	/* Send the cmd, will set as cur_cmd in the send function */
	if (asp_trx_send_cmd(cmd, cmd->dest) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send queued cmd(%u)\n",
			   cmd->seq);
		cmd->res = FAIL_SEND_FAILED;
		asp_trx_cmd_complete(cmd);
		return;
	}

	wpa_printf(MSG_ERROR, ASP_TAG": Queued cmd(%u) sent\n", cmd->seq);
}

static void asp_trx_cmd_complete(struct asp_trx_cmd *cmd)
{
	struct asp_trx_info *trx = cmd->trx;

	WPA_ASSERT(cmd == trx->cur_cmd);

	/* Cancel all timeout related to the cmd */
	eloop_cancel_timeout(asp_trx_cmd_resp_timeout, cmd, ELOOP_ALL_CTX);
	eloop_cancel_timeout(asp_trx_cmd_age_func, cmd, ELOOP_ALL_CTX);

	/* Invoke completion routine */
	trx->cur_cmd = NULL;
	cmd->completion(cmd, cmd->completion_ctx);

	/* Check if we have queued cmd */
	if (dl_list_empty(&trx->cmd_queue))
		return;

	/* Process queued cmd */
	eloop_register_timeout(0, 0, asp_trx_process_queued_cmd, trx, NULL);
}

static void asp_trx_cmd_age_func(void *eloop_data, void *user_ctx)
{
	struct asp_trx_cmd *cmd = (struct asp_trx_cmd *)eloop_data;
	struct asp_trx_info *trx = cmd->trx;

	/* For cur_cmd, leave it to the resp timeout function */
	if (trx->cur_cmd == cmd)
		return;

	/* Check if the cmd is still in the queue */
	if (!asp_trx_cmd_in_queue(trx, cmd))
		return;

	wpa_printf(MSG_INFO,
		   ASP_TAG": Aged out cmd(%u) with res: %d, flag: %u\n",
		   cmd->seq, cmd->res, cmd->flag);

	/* Invoke completion with failure */
	cmd->res = FAIL_AGED_OUT;
	asp_trx_cmd_complete(cmd);
}

static void asp_trx_cmd_resp_timeout(void *eloop_data, void *user_ctx)
{
	struct asp_trx_cmd *cmd = (struct asp_trx_cmd *)eloop_data;

	WPA_ASSERT(cmd);
	WPA_ASSERT(cmd == cmd->trx->cur_cmd);
	WPA_ASSERT(!(cmd->flag & ASP_TRX_CMD_FLAG_RESP_RECVD));

	wpa_printf(MSG_INFO, ASP_TAG": resp timeout for cmd(%u)\n", cmd->seq);

	cmd->res = FAIL_NO_RESP;

	if (cmd->attempts < 8) {
		/* Retry */
		if (asp_trx_sock_send(cmd->trx->sd,
				       cmd->dest, cmd->buf) < 0) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": Failed to send cmd(%u)\n",
				   cmd->seq);
			cmd->res = FAIL_SEND_FAILED;
			goto fail;
		}
		cmd->flag |= ASP_TRX_CMD_FLAG_RETRY;
		cmd->attempts++;
		eloop_register_timeout(ASP_TRX_ACK_TO_SEC, 0,
				       asp_trx_cmd_resp_timeout, cmd, NULL);

		wpa_printf(MSG_INFO, ASP_TAG": retry cmd(%u), attempt: %d, \
flag: 0x%08X\n",
			   cmd->seq, (int)cmd->attempts, cmd->flag);
	} else {
		wpa_printf(MSG_INFO,
			   ASP_TAG
			   ": retry limit reached for cmd(%u), give up\n",
			   cmd->seq);
		goto fail;
	}

	return;

fail:
	/* Failed to send the cmd, invoke completion with failure */
	asp_trx_cmd_complete(cmd);
}

//------------------------------------------------------------------------------
//
// Local functions: rx
//
//------------------------------------------------------------------------------
static void asp_trx_dispatch_resp(struct asp_trx_info *trx,
				  u8 opcode, u8 seq, struct wpabuf *payload)
{
	struct asp_trx_cmd *cmd;

	/* Get the pending cmd */
	if (trx->cur_cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": rx resp but no cur_cmd\n");
		return;
	}

	cmd = trx->cur_cmd;

	/* Check seq */
	if (cmd->seq != seq) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": Mismatch resp(%u) and last tx cmd(%u), drop\n",
			   seq, cmd->seq);
		return;
	}

	/* Check state */
	if (!(cmd->flag & ASP_TRX_CMD_FLAG_SENT) ||
	    cmd->flag & ASP_TRX_CMD_FLAG_RESP_RECVD) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": rx resp but corresp. cmd is in invalid \
state: 0x%08X\n",
			   cmd->flag);
		return;
	}

	wpa_printf(MSG_INFO, "rx resp for cmd(%u)\n", cmd->seq);

	cmd->flag |= ASP_TRX_CMD_FLAG_RESP_RECVD;

	/* Determine result */
	if (opcode == ASP_MSG_ACK)
		cmd->res = ACKED;
	else {
		cmd->res = NACKED;
	        cmd->nack_reason = (1 <= wpabuf_len(payload)) ?
			*wpabuf_head_u8(payload) : 0;
	}

	/* Invoke completion */
	asp_trx_cmd_complete(cmd);
}

static void asp_trx_dispatch_cmd(struct asp_trx_info *trx,
				 u8 opcode, u8 seq, struct wpabuf *payload)
{
	if (trx->cmd_hdlr) {
		trx->cmd_hdlr(trx->cmd_ctx, opcode, seq, payload);
	}
}

static void asp_trx_process_ver(struct asp_trx_info *trx,
				u8 opcode, u8 seq, struct wpabuf *payload)
{
	struct wpabuf *buf;

	wpa_printf(MSG_INFO, ASP_TAG": process ver(%u)\n", seq);

	/* Send ack */
	buf = wpabuf_alloc(16);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate buf for ack\n");
		goto fail;
	}

	wpabuf_put_u8(buf, ASP_MSG_ACK);
	wpabuf_put_u8(buf, seq);

	if (asp_trx_send_resp(trx, trx->remote_ip, buf) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send ack\n");
		goto fail;
	}

	wpabuf_free(buf);

	/* Set flag */
	trx->flag |= ASP_TRX_FLAG_VER_RECVD;
	if (!(trx->flag & ASP_TRX_FLAG_VER_DONE) &&
	    trx->flag & ASP_TRX_FLAG_VER_SENT) {
		asp_trx_on_ver_exchg_complete(trx);
	}

	return;

fail:
	if (buf)
		wpabuf_free(buf);
}

static void asp_trx_rx(int sd, void *eloop_ctx, void *sock_ctx)
{
	struct asp_trx_info *trx = (struct asp_trx_info *)eloop_ctx;
	size_t nread;
	u8 opcode, seq;
	const u8 *payload_data;
	struct wpabuf payload;

	/* Check state */
	if (!asp_trx_sock_inited(trx))
		return;

	/* Receive */
	nread = asp_trx_sock_recv(trx->sd, trx->rxbuf);

	/* Check length */
	if (nread < 2) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": rx data w/ invalid len: %d, drop\n",
			   (int)nread);
		if (nread <= 0) {
			/* TODO: remove session */
		}
		return;
	}

	/* Get opcode and seq */
	opcode = *(wpabuf_head_u8(trx->rxbuf));
	seq = *(wpabuf_head_u8(trx->rxbuf) + 1);
	wpa_printf(MSG_INFO,
		   ASP_TAG": rx msg, opcode: %u, seq: %u\n",
		   opcode, seq);

	/* Strip opcode and seq */
	payload_data = (2 < wpabuf_len(trx->rxbuf)) ?
		wpabuf_head_u8(trx->rxbuf) + 2 : NULL;
	if (payload_data == NULL)
		wpabuf_set(&payload, NULL, 0);
	else
		wpabuf_set(&payload, payload_data,
			   (size_t)(wpabuf_head_u8(trx->rxbuf) +
				    wpabuf_len(trx->rxbuf) - payload_data));

	/* Dispatch msg */
	if (opcode == ASP_MSG_ACK ||
	    opcode == ASP_MSG_NACK) {
		asp_trx_dispatch_resp(trx, opcode, seq, &payload);
	} else if (opcode == ASP_MSG_VERSION) {
		asp_trx_process_ver(trx, opcode, seq, &payload);
	} else {
		asp_trx_dispatch_cmd(trx, opcode, seq, &payload);
	}

	return;
}

//------------------------------------------------------------------------------
//
// Local functions: version
//
//------------------------------------------------------------------------------
static void asp_trx_on_init_complete(struct asp_trx_info *trx)
{
	/* Invoke completion */
	if (trx->init_complete_cb)
		trx->init_complete_cb(trx->init_complete_ctx);
}

static void asp_trx_on_ver_exchg_complete(struct asp_trx_info *trx)
{
	/* Check if version exchange has already done */
	if (trx->flag & ASP_TRX_FLAG_VER_DONE)
		return;

	wpa_printf(MSG_INFO, ASP_TAG": Version exchange complete\n");

	/* Determine version exchange succeeded or failed */
	if (trx->flag & ASP_TRX_FLAG_VER_SENT &&
	    trx->flag & ASP_TRX_FLAG_VER_RECVD)
		trx->flag |= ASP_TRX_FLAG_VER_DONE;

	eloop_cancel_timeout(asp_trx_ver_exchg_timeout, trx, ELOOP_ALL_CTX);
	asp_trx_on_init_complete(trx);
}

static void asp_trx_build_ver(struct wpabuf *buf, u8 seq, u8 ver)
{
	/* opcode */
	wpabuf_put_u8(buf, 5);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* ver */
	wpabuf_put_u8(buf, ver);

	/* vendor info length */
	wpabuf_put_u8(buf, 0);
}

static void asp_trx_send_ver_complete(struct asp_trx_cmd *cmd, void *ctx)
{
	struct asp_trx_info *trx = (struct asp_trx_info *)ctx;

	wpa_printf(MSG_INFO,
		   ASP_TAG": send ver cmd(%u) completed with result: %d, \
flag: 0x%08X\n",
		   cmd->seq, cmd->res, cmd->flag);

	if (cmd->res == ACKED)
		trx->flag |= ASP_TRX_FLAG_VER_SENT;

	asp_trx_free_cmd(cmd);

	/* Check ver exchange complete */
	if (!(trx->flag  & ASP_TRX_FLAG_VER_DONE) &&
	    trx->flag & ASP_TRX_FLAG_VER_RECVD) {
		asp_trx_on_ver_exchg_complete(trx);
	}
}

static void asp_trx_ver_exchg_timeout(void *eloop_data, void *user_ctx)
{
	struct asp_trx_info *trx = (struct asp_trx_info *)eloop_data;

	wpa_printf(MSG_ERROR, ASP_TAG": Version exchange timeout\n");
	asp_trx_on_ver_exchg_complete(trx);
}

static int asp_trx_send_ver(struct asp_trx_info *trx)
{
	struct asp_trx_cmd *cmd;

	/* Alloc cmd */
	cmd = asp_trx_alloc_cmd(trx, 16,
				asp_trx_send_ver_complete, trx);
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate cmd for ver\n");
		goto fail;
	}

	cmd->flag |= ASP_TRX_FLAG_INTERNAL_CMD;

	/* Build ver */
	asp_trx_build_ver(cmd->buf, cmd->seq, ASP_VER);

	/* Send ver */
	if (asp_trx_send_cmd(cmd, trx->remote_ip) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send ver\n");
		goto fail;
	}

	/* Register ver exchg timeout */
	eloop_register_timeout(ASP_TRX_VER_EXCHG_TO_SEC, 0,
			       asp_trx_ver_exchg_timeout, trx, NULL);

	return 0;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return -1;
}

//------------------------------------------------------------------------------
//
// Exported functions
//
//------------------------------------------------------------------------------

int asp_trx_init(struct asp_trx_info *trx, struct in_addr local_ip,
		 struct in_addr remote_ip, asp_trx_cmd_hdlr cmd_hdlr,
		 void *cmd_ctx, asp_trx_init_complete init_complete_cb,
		 void *init_complete_ctx)
{
	os_memset(trx, 0, sizeof(*trx));

	/* Init var */
	trx->sd = -1;
	trx->local_ip = local_ip;
	trx->remote_ip = remote_ip;
	trx->init_complete_cb = init_complete_cb;
	trx->init_complete_ctx = init_complete_ctx;
	dl_list_init(&trx->cmd_queue);
	trx->cmd_hdlr = cmd_hdlr;
	trx->cmd_ctx = cmd_ctx;
	trx->next_seq = 0;

	/* Allocate rx buf */
	trx->rxbuf = wpabuf_alloc(4096);
	if (trx->rxbuf == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to allocate rxbuf\n");
		goto fail;
	}

	/* Init sock */
	asp_trx_init_sock(trx, local_ip);
	if (trx->sd < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to init sock\n");
		goto fail;
	}

	/* Register socket rx callback */
	if (eloop_register_read_sock(trx->sd, asp_trx_rx, trx, NULL) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to register read sock\n");
		goto fail;
	}

	/* Mark as initializing */
	trx->flag |= ASP_TRX_FLAG_INITIALIZED;
	wpa_printf(MSG_INFO, ASP_TAG": TRX initialized, start ver exchg\n");

	/* Start version exchg */
	if (asp_trx_send_ver(trx) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send ver\n");
		goto fail;
	}

	return 0;

fail:
	if(0 <= trx->sd)
		eloop_unregister_read_sock(trx->sd);

	asp_trx_deinit_sock(trx);

	if (trx->rxbuf)
		wpabuf_free(trx->rxbuf);

	return -1;
}

int asp_trx_started(struct asp_trx_info *trx)
{
	if (!asp_trx_sock_inited(trx))
		return 0;
	if (!(trx->flag & ASP_TRX_FLAG_VER_DONE))
		return 0;
	return 1;
}

void asp_trx_deinit(struct asp_trx_info *trx)
{
	/* Check state */
	if (!asp_trx_sock_inited(trx))
		return;

	/* Cancel all timer related to trx */
	eloop_cancel_timeout(asp_trx_ver_exchg_timeout, trx, ELOOP_ALL_CTX);
	eloop_cancel_timeout(asp_trx_process_queued_cmd, trx, ELOOP_ALL_CTX);

	/* Cancel ver exchg */
	asp_trx_cancel_cmd(trx, asp_trx_send_ver_complete, trx);

	/* Free all cmds, completion will not be called */
	asp_trx_free_all_cmd(trx);

	/* Unregister read sock */
	if (0 < trx->sd)
		eloop_unregister_read_sock(trx->sd);

	/* Close sock */
	asp_trx_deinit_sock(trx);

	/* Free rx buf */
	if (trx->rxbuf) {
		wpabuf_free(trx->rxbuf);
		trx->rxbuf = NULL;
	}

	/* Mark as de-initialized */
	trx->flag |= ASP_TRX_FLAG_DEINITIALIZED;
	wpa_printf(MSG_INFO, ASP_TAG": TRX deinitialized\n");
}

struct asp_trx_cmd * asp_trx_alloc_cmd(struct asp_trx_info *trx, size_t buflen,
				       asp_trx_cmd_send_complete completion,
				       void *completion_ctx)
{
	struct asp_trx_cmd *cmd;

	/* Allocate cmd */
	cmd = os_zalloc(sizeof(*cmd));
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to allocate cmd\n");
		goto fail;
	}

	/* Allocate buf */
	cmd->buf = wpabuf_alloc(buflen);
	if (cmd->buf == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to allocate cmd buf\n");
		goto fail;
	}

	/* Fill info */
	cmd->seq = trx->next_seq++;
	cmd->completion = completion;
	cmd->completion_ctx = completion_ctx;
	cmd->trx = trx;
	cmd->res = INVALID_RES;

	return cmd;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return NULL;
}

void asp_trx_free_cmd(struct asp_trx_cmd *cmd)
{
	if (cmd == NULL)
		return;

	wpa_printf(MSG_INFO,
		   ASP_TAG": Freeing cmd(%u) with flag: 0x%08X\n",
		   cmd->seq, cmd->flag);

	/* All related timer are canceled by @asp_trx_cmd_complete */

	/* Free tx buf */
	if (cmd->buf) {
		wpabuf_free(cmd->buf);
		cmd->buf = NULL;
	}

	/* Free the cmd */
	os_free(cmd);
}

int asp_trx_send_cmd(struct asp_trx_cmd *cmd, struct in_addr dest)
{
	struct asp_trx_info *trx = cmd->trx;

	/* Check state */
	if (!asp_trx_sock_inited(trx))
		return -1;
	if (!(cmd->flag & ASP_TRX_FLAG_INTERNAL_CMD))
		if (!asp_trx_started(trx))
		    return -1;

	/* Always need a completion routine */
	if (cmd->completion == NULL)
		return -1;

	/* Queue the cmd if there's a current cmd */
	if (trx->cur_cmd) {
		cmd->flag |= ASP_TRX_CMD_FLAG_DEFERRED;
		dl_list_add_tail(&trx->cmd_queue, &cmd->list);
		eloop_register_timeout(10, 0, asp_trx_cmd_age_func, cmd, NULL);
		wpa_printf(MSG_INFO, ASP_TAG
			   ": cmd(%u) deferred, will send after cmd(%u) \
completed\n"
			   , cmd->seq, trx->cur_cmd->seq);
		return 0;
	}

	/* Send cmd */
	if (asp_trx_sock_send(trx->sd, dest, cmd->buf) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send cmd(%u)\n",
			   cmd->seq);
		cmd->res = FAIL_SEND_FAILED;
		return -1;
	}

	wpa_printf(MSG_INFO, ASP_TAG": cmd(%u) sent, flag: 0x%08X\n",
		   cmd->seq, cmd->flag);

	cmd->attempts++;
	cmd->flag |= ASP_TRX_CMD_FLAG_SENT;
	cmd->dest = dest;

	/* Wait resp */
	eloop_register_timeout(ASP_TRX_ACK_TO_SEC, 0,
			       asp_trx_cmd_resp_timeout, cmd, NULL);

	/* Set as cur_cmd */
	trx->cur_cmd = cmd;

	return 0;
}

int asp_trx_cancel_cmd(struct asp_trx_info *trx,
		       asp_trx_cmd_send_complete completion,
		       void *completion_ctx)
{
	struct asp_trx_cmd *cur, *cmd, *tmp;
	int removed = 0;

	/* Check cur_cmd */
	if (trx->cur_cmd &&
	    trx->cur_cmd->completion == completion &&
	    trx->cur_cmd->completion_ctx == completion_ctx) {
		cur = trx->cur_cmd;
		if (asp_trx_cancel_cur_cmd(trx) == 0) {
			asp_trx_free_cmd(cur);
			removed++;
		}
	}

	/* Check cmd queue */
	dl_list_for_each_safe(cmd, tmp, &trx->cmd_queue, struct asp_trx_cmd,
			      list) {
		if (cmd->completion == completion &&
		    cmd->completion_ctx == completion_ctx) {
			if (asp_trx_cancel_queued_cmd(trx, cmd) == 0) {
				asp_trx_free_cmd(cmd);
				removed++;
			}
		}
	}

	return removed;
}

int asp_trx_send_resp(struct asp_trx_info *trx, struct in_addr dest,
		      struct wpabuf *buf)
{
	/* Send */
	if (asp_trx_sock_send(trx->sd, dest, buf) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send resp\n");
		return -1;
	}

	return 0;
}
