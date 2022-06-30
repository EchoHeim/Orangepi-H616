//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//

#include "rt_asp_i.h"

const char * asp_prot_magic = "ASP_PROT";

#define ASP_PROT_OPEN_SESSION_TO_SEC 10
//#define ASP_PROT_RM_SESSION_TO_SEC 1

static void asp_prot_open_session_timeout(void *eloop_data, void *user_ctx);
static int asp_prot_send_req_session(struct asp_prot_info *info);
static int asp_prot_send_added_session(struct asp_prot_info *info);
static void asp_prot_send_added_session_complete(struct asp_trx_cmd *cmd,
						 void *ctx);
static void asp_prot_send_req_session_complete(struct asp_trx_cmd *cmd,
					       void *ctx);
static void asp_prot_send_rm_session_complete(struct asp_trx_cmd *cmd,
					      void *ctx);

void asp_prot_dump_bound_ports(struct asp_prot_info *info)
{
	struct asp_bound_port_entry *e;
	int cnt = 0;

	dl_list_for_each(e, &info->bound_port_list,
			 struct asp_bound_port_entry, list) {
		wpa_printf(MSG_INFO, ASP_TAG": --- Bound Port #%d ---\n", cnt++);
		wpa_printf(MSG_INFO, ASP_TAG": port: %u\n", e->port);
		wpa_printf(MSG_INFO, ASP_TAG": prot: %u\n", e->proto);
		wpa_printf(MSG_INFO, ASP_TAG": ip: %s\n", e->ip_txt);
	}
}


//------------------------------------------------------------------------------
//
// Internal event handler
//
//------------------------------------------------------------------------------
static void asp_prot_on_open_session_succeeded(struct asp_prot_info *info)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx = entry->ctx;
	char local_ip_txt[16];
	char remote_ip_txt[16];

	wpa_printf(MSG_INFO, ASP_TAG": Open session succeeded\n");

	/* Cancel timeout */
	eloop_cancel_timeout(asp_prot_open_session_timeout, info, NULL);

	asp_set_session_state(entry, SESSION_OPENED);

	/* Send Session Status event */
	if (entry->open_by_prov_disc == 0)
		wpa_msg_global(ctx->wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=SessionRequestAccepted",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);

	/* Make IP txt */
	os_snprintf(local_ip_txt, sizeof(local_ip_txt), "%s", inet_ntoa(entry->ip_info.local_ip));
	os_snprintf(remote_ip_txt, sizeof(remote_ip_txt), "%s", inet_ntoa(entry->ip_info.remote_ip));

	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-SESSION-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " state=Open"
		       " status=OK"
		       " local_ip=%s"
		       " remote_ip=%s",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id,
		       local_ip_txt, remote_ip_txt);
}

static void asp_prot_on_open_session_failed(struct asp_prot_info *info)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx = entry->ctx;

	wpa_printf(MSG_INFO, ASP_TAG": Open session failed\n");

	/* Cancel timeout */
	eloop_cancel_timeout(asp_prot_open_session_timeout, info, NULL);

	/* Change state */
	asp_set_session_state(entry, SESSION_CLOSED);

	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-SESSION-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " state=Closed"
		       " status=TODO",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id);
}

static void asp_prot_on_session_removed(struct asp_prot_info *info)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx = entry->ctx;
	struct asp_trx_info *trx = info->trx;

	wpa_printf(MSG_INFO, ASP_TAG": Session removed\n");

	/* Set flag */
	info->flag |= ASP_PROT_FLAG_SESSION_REMOVED;

	/* Cancel all timeout related to the prot info */
	eloop_cancel_timeout(asp_prot_open_session_timeout,
			     info, ELOOP_ALL_CTX);

	/* Cancel all outstanding cmds */
	asp_trx_cancel_cmd(trx, asp_prot_send_added_session_complete, info);
	asp_trx_cancel_cmd(trx, asp_prot_send_req_session_complete, info);
	asp_trx_cancel_cmd(trx, asp_prot_send_rm_session_complete, info);

	/* Set state to session closed so we can open it again */
	asp_set_session_state(entry, SESSION_CLOSED);

	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-SESSION-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " state=Closed"
		       " status=TODO",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id);

	if (asp_get_session_entry_by_state(ctx, SESSION_OPENED) == NULL) {
		wpa_msg_global(ctx->wpa_s, MSG_INFO,
			       "ASP-STATUS"
			       " status=AllSessionClosed");
	}
}

//------------------------------------------------------------------------------
//
// Local functions: ASP coordination protocol
//
//------------------------------------------------------------------------------
static void asp_build_req_session(struct wpabuf *buf, u8 seq,
				  const u8 *session_mac, u32 session_id,
				  u32 adv_id, u8 session_info_len,
				  const void *session_info)
{
	/* opcode */
	wpabuf_put_u8(buf, 0);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* session mac */
	wpabuf_put_data(buf, session_mac, ETH_ALEN);

	/* session id */
	wpabuf_put_be32(buf, session_id);

	/* adv id */
	wpabuf_put_be32(buf, adv_id);

	/* session info len */
	wpabuf_put_u8(buf, session_info_len);

	/* session info */
	wpabuf_put_data(buf, session_info, session_info_len);
}

static void asp_build_added_session(struct wpabuf *buf, u8 seq,
				    const u8 *session_mac, u32 session_id)
{
	/* opcode */
	wpabuf_put_u8(buf, 1);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* session mac */
	wpabuf_put_data(buf, session_mac, ETH_ALEN);

	/* session id */
	wpabuf_put_be32(buf, session_id);
}

static void asp_build_rm_session(struct wpabuf *buf, u8 seq,
				 const u8 *session_mac, u32 session_id,
				 u8 reason)
{
	/* opcode */
	wpabuf_put_u8(buf, 3);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* session mac */
	wpabuf_put_data(buf, session_mac, ETH_ALEN);

	/* session id */
	wpabuf_put_be32(buf, session_id);

	/* reason */
	wpabuf_put_u8(buf, reason);
}

static void asp_build_allowed_port(struct wpabuf *buf, u8 seq,
				   const u8 *session_mac, u32 session_id,
				   u16 port, u8 proto)
{
	/* opcode */
	wpabuf_put_u8(buf, 4);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* session mac */
	wpabuf_put_data(buf, session_mac, ETH_ALEN);

	/* session id */
	wpabuf_put_be32(buf, session_id);

	/* port */
	wpabuf_put_be16(buf, port);

	/* proto */
	wpabuf_put_u8(buf, proto);
}

static int asp_send_ack(struct asp_prot_info *info, struct in_addr dest, u8 seq)
{
	struct wpabuf *buf;

	/* Allocate buf for ack */
	buf = wpabuf_alloc(16);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate buf for ack\n");
		goto fail;
	}

	/* opcode */
	wpabuf_put_u8(buf, ASP_MSG_ACK);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* send */
	if (asp_trx_send_resp(info->trx, dest, buf) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send ack\n");
		goto fail;
	}

	wpabuf_free(buf);

	return 0;

fail:
	if (buf)
		wpabuf_free(buf);

	return -1;
}

static int asp_send_nack(struct asp_prot_info *info, struct in_addr dest,
			 u8 seq, u8 reason)
{
	struct wpabuf *buf;

	/* Allocate buf for ack */
	buf = wpabuf_alloc(16);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate buf for nack\n");
		goto fail;
	}

	/* opcode */
	wpabuf_put_u8(buf, ASP_MSG_NACK);

	/* seq */
	wpabuf_put_u8(buf, seq);

	/* reason */
	wpabuf_put_u8(buf, reason);

	/* send */
	if (asp_trx_send_resp(info->trx, dest, buf) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send nack\n");
		goto fail;
	}

	wpabuf_free(buf);

	return 0;

fail:
	if (buf)
		wpabuf_free(buf);

	return -1;
}

//------------------------------------------------------------------------------
//
// Local functions: RX
//
//------------------------------------------------------------------------------
static void asp_prot_process_req_session(struct asp_prot_info *info, u8 opcode,
					 u8 seq, const u8 *session_mac,
					 u32 session_id, struct wpabuf *payload)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx = entry->ctx;
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	struct asp_msg msg;

	wpa_printf(MSG_INFO, ASP_TAG": process req_session(%u)\n", seq);

	/* Check state */
	if (info->flag & ASP_PROT_FLAG_ADDED_SESSION_SENT) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": invalid session state, flag: 0x%08X\n",
			   info->flag);
		goto send_nack;
	}

	if (asp_parse_req_session(payload, &msg) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to parse req_session(%u)\n",
			   seq);
		goto send_nack;
	}

	info->flag |= ASP_PROT_FLAG_REQ_SESSION_RECVD;

	/* Send ack */
	if (asp_send_ack(info, entry->ip_info.remote_ip, seq) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send ack for req_session(%u)\n",
			   seq);
		return;
	}

	/* If already in opened state, simply send ack and return */
	if (entry->state == SESSION_OPENED)
		return;

	/* Send added session */
	if (asp_prot_send_added_session(info) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send added session\n");
		return;
	}

	info->flag |= ASP_PROT_FLAG_ADDED_SESSION_SENT;

	/* Send SessionRequest event */
	if (entry->open_by_prov_disc == -1) {
		entry->open_by_prov_disc = 0;
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-SESSION-REQ"
			       " adv_id=%u"
			       " session_mac=" MACSTR
			       " service_device_name='%s'"
			       " session_id=%u"
			       " session_info=\'%.*s\'",
			       *((const u32 *)(wpabuf_head(payload) + 0)),
			       MAC2STR(session_mac), "TODO-dev-name",
			       session_id, *msg.session_info_len,
			       msg.session_info);

		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNEXT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=SessionRequestReceived",
			       MAC2STR(session_mac), session_id);
	}

	return;

send_nack:
	asp_send_nack(info, entry->ip_info.remote_ip, seq, 0x04);
}

static void asp_prot_process_added_session(struct asp_prot_info *info,
					   u8 opcode, u8 seq,
					   const u8 *session_mac,
					   u32 session_id,
					   struct wpabuf *payload)
{
	struct asp_session *entry = info->entry;

	wpa_printf(MSG_INFO, ASP_TAG": process added_session(%u)\n", seq);

	/* Check state */
	if (!(info->flag & ASP_PROT_FLAG_REQ_SESSION_SENT) ||
	    info->flag & ASP_PROT_FLAG_ADDED_SESSION_RECVD) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": invalid session state, flag: 0x%08X\n",
			   info->flag);
		asp_send_nack(info, entry->ip_info.remote_ip, seq, 0x04);
		return;
	}

	info->flag |= ASP_PROT_FLAG_ADDED_SESSION_RECVD;

	/* Send ack */
	if (asp_send_ack(info, entry->ip_info.remote_ip, seq) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG
			   ": Failed to send ack for added_session(%u)\n",
			   seq);
		return;
	}

	/* Check open session complete */
	if (entry->state == SESSION_CLOSED) {
		asp_prot_on_open_session_succeeded(info);
	} else {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Invalid session state: %s\n",
			   asp_session_state_txt(entry->state));
		return;
	}

	return;
}

static void asp_prot_process_rm_session(struct asp_prot_info *info, u8 opcode,
					u8 seq, const u8 *session_mac,
					u32 session_id, struct wpabuf *payload)
{
	struct asp_session *entry = info->entry;

	wpa_printf(MSG_INFO, ASP_TAG": process rm_session(%u)\n", seq);

	/* Check state */
	if (entry->state != SESSION_OPENED) {
		/* Socket established, not session not yet opened */
		wpa_printf(MSG_ERROR,
			   ASP_TAG": invalid session state: %s, flag: 0x%08X\n",
			   asp_session_state_txt(entry->state), info->flag);
		asp_send_nack(info, entry->ip_info.remote_ip, seq, 0x04);
		return;
	}

	/* Send ack */
	if (asp_send_ack(info, entry->ip_info.remote_ip, seq) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG
			   ": Failed to send ack for rm session(%u), go on to \
rm session\n",
			   seq);
	}

	/* Mark session as removed */
	asp_prot_on_session_removed(info);

	return;
}

static void asp_prot_process_allowed_port(struct asp_prot_info *info, u8 opcode,
					  u8 seq, const u8 *session_mac,
					  u32 session_id,
					  struct wpabuf *payload)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx;
	u16 port;
	u8 proto;
	char ip_txt[16];

	wpa_printf(MSG_INFO, ASP_TAG": process allowed_port(%u)\n", seq);

	/* Check state */
	if (entry->state != SESSION_OPENED) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": invalid session state: %s, flag: 0x%08X\n",
			   asp_session_state_txt(entry->state), info->flag);
		asp_send_nack(info, entry->ip_info.remote_ip, seq, 0x04);
		return;
	}

	ctx = entry->ctx;

	/* Parse port and proto */
	port = WPA_GET_BE16(wpabuf_head_u8(payload));
	proto = *(wpabuf_head_u8(payload) + 2);

	/* Send ack */
	if (asp_send_ack(info, entry->ip_info.remote_ip, seq) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG
			   ": Failed to send ack for allowed port(%u)\n", seq);
	}

	os_snprintf(ip_txt, sizeof(ip_txt), "%s",
		    inet_ntoa(entry->ip_info.remote_ip));
	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-PROT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " port=%u proto=%u ip=%s status=RemotePortAllowed",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id, port, proto, ip_txt);

	return;
}

static int asp_prot_filter_dup_msg(struct asp_prot_info *info, u8 opcode,
				   u8 seq)
{
	struct asp_session *entry = info->entry;

	if (seq <= info->last_rx_seq) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": drop duplicated cmd(%u), opcode %u for \
it is less or equal to %u\n",
			   seq, opcode, info->last_rx_seq);
		asp_send_ack(info, entry->ip_info.remote_ip, seq);
		return 1;
	}

	/* Update rx seq */
	info->last_rx_seq = (seq == 0xFF) ? -1 : seq;

	return 0;
}

//------------------------------------------------------------------------------
//
// Local functions: process
//
//------------------------------------------------------------------------------
static void asp_prot_send_added_session_complete(struct asp_trx_cmd *cmd,
						 void *ctx)
{
	struct asp_prot_info *info = (struct asp_prot_info *)ctx;

	wpa_printf(MSG_INFO, ASP_TAG": send added_session(%u) completed \
with result: %d\n",
		   cmd->seq, cmd->res);

	if (cmd->res == ACKED) {
		info->flag |= ASP_PROT_FLAG_ADDED_SESSION_SENT;
		asp_prot_on_open_session_succeeded(info);
	}
	else {
		asp_prot_on_open_session_failed(info);
	}

	asp_trx_free_cmd(cmd);
}

static int asp_prot_send_added_session(struct asp_prot_info *info)
{
	struct asp_session *entry = info->entry;
	struct asp_trx_info *trx = info->trx;
	struct asp_trx_cmd *cmd;

	/* Alloc cmd */
	cmd = asp_trx_alloc_cmd(trx, 256,
				asp_prot_send_added_session_complete, info);
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG
			   ": Failed to allocate cmd for added session\n");
		goto fail;
	}

	/* Build cmd */
	asp_build_added_session(cmd->buf, cmd->seq,
				entry->p2ps_prov->session_mac,
				entry->p2ps_prov->session_id);

	/* Send the cmd */
	if (asp_trx_send_cmd(cmd, entry->ip_info.remote_ip) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send added session\n");
		goto fail;
	}

	/* Register added session timeout */
	eloop_register_timeout(ASP_PROT_OPEN_SESSION_TO_SEC, 0,
			       asp_prot_open_session_timeout, info, NULL);

	return 0;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return -1;
}

static void asp_prot_send_req_session_complete(struct asp_trx_cmd *cmd,
					       void *ctx)
{
	struct asp_prot_info *info = (struct asp_prot_info *)ctx;
	struct asp_session *entry = info->entry;
	struct wpa_supplicant *wpa_s = entry->ctx->wpa_s;

	wpa_printf(MSG_INFO,
		   ASP_TAG": send req_session(%u) completed with \
result: %d\n",
		   cmd->seq, cmd->res);

	/* Send ConnectionStatus event */
	if (entry->open_by_prov_disc == 0) {
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=%s",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id,
			       cmd->res == ACKED ? "SessionRequestSent" :
			       "SessionRequestFailed");
	}


	if (cmd->res == ACKED) {
		info->flag |= ASP_PROT_FLAG_REQ_SESSION_SENT;
	} else {
		asp_prot_on_open_session_failed(info);
	}

	asp_trx_free_cmd(cmd);
}

static void asp_prot_open_session_timeout(void *eloop_data, void *user_ctx)
{
	struct asp_prot_info *info = (struct asp_prot_info *)eloop_data;
	struct asp_session *entry = info->entry;

	wpa_printf(MSG_INFO,
		   ASP_TAG": open session timeout, current state: %s\n",
		   asp_session_state_txt(entry->state));

	return;
}

static int asp_prot_send_req_session(struct asp_prot_info *info)
{
	struct asp_session *entry = info->entry;
	struct asp_trx_info *trx = info->trx;
	struct asp_trx_cmd *cmd;

	/* Alloc cmd */
	cmd = asp_trx_alloc_cmd(trx, 256,
				asp_prot_send_req_session_complete, info);
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate cmd for req session\n");
		goto fail;
	}

	/* Build cmd */
	asp_build_req_session(cmd->buf, cmd->seq,
			      entry->p2ps_prov->session_mac,
			      entry->p2ps_prov->session_id,
			      entry->p2ps_prov->adv_id,
			      (u8)os_strlen(entry->p2ps_prov->info),
			      entry->p2ps_prov->info);

	/* Send the cmd */
	if (asp_trx_send_cmd(cmd, entry->ip_info.remote_ip) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send req session\n");
		goto fail;
	}

	/* Register added session timeout */
	eloop_register_timeout(ASP_PROT_OPEN_SESSION_TO_SEC, 0,
			       asp_prot_open_session_timeout, info, NULL);

	return 0;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return -1;
}

static void asp_prot_send_rm_session_complete(struct asp_trx_cmd *cmd,
					      void *ctx)
{
	struct asp_prot_info *info = (struct asp_prot_info *)ctx;

	wpa_printf(MSG_INFO,
		   ASP_TAG": send rm session(%u) completed with result: %d, \
flag: 0x%08X\n",
		   cmd->seq, cmd->res, cmd->flag);

	/* Mark session as removed, don't care if ack is received */
	asp_prot_on_session_removed(info);

	asp_trx_free_cmd(cmd);
}

static int asp_prot_send_rm_session(struct asp_prot_info *info, u8 reason)
{
	struct asp_session *entry = info->entry;
	struct asp_trx_info *trx = info->trx;
	struct asp_trx_cmd *cmd;

	/* Check if session opened */
	if (entry->state != SESSION_OPENED) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": can't remove session when session is not \
opened\n");
		return -1;
	}

	/* Alloc cmd */
	cmd = asp_trx_alloc_cmd(trx, 16,
				asp_prot_send_rm_session_complete, info);
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate cmd for remove \
session\n");
		goto fail;
	}

	/* Build cmd */
	asp_build_rm_session(cmd->buf, cmd->seq,
			     entry->p2ps_prov->session_mac,
			     entry->p2ps_prov->session_id,
			     reason);

	/* Send the cmd */
	if (asp_trx_send_cmd(cmd, entry->ip_info.remote_ip) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send rm session\n");
		goto fail;
	}

	return 0;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return -1;
}

static void asp_prot_send_allowed_port_complete(struct asp_trx_cmd *cmd,
					      void *ctx)
{
	wpa_printf(MSG_INFO,
		   ASP_TAG": send allowed port(%u) completed with result: %d, \
flag: 0x%08X\n",
		   cmd->seq, cmd->res, cmd->flag);

	asp_trx_free_cmd(cmd);
}

static int asp_prot_send_allowed_port(struct asp_prot_info *info, u16 port,
				      u8 proto)
{
	struct asp_session *entry = info->entry;
	struct asp_trx_info *trx = info->trx;
	struct asp_trx_cmd *cmd;

	/* Check if session opened */
	if (entry->state != SESSION_OPENED) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": can't set allowed port when session is not\
opened\n");
		return -1;
	}

	/* Alloc cmd */
	cmd = asp_trx_alloc_cmd(trx, 16,
				asp_prot_send_allowed_port_complete, info);
	if (cmd == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate cmd for allowed \
port\n");
		goto fail;
	}

	/* Build cmd */
	asp_build_allowed_port(cmd->buf, cmd->seq,
			       entry->p2ps_prov->session_mac,
			       entry->p2ps_prov->session_id, port, proto);

	/* Send the cmd */
	if (asp_trx_send_cmd(cmd, entry->ip_info.remote_ip) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to send allowed port\n");
		goto fail;
	}

	return 0;

fail:
	if(cmd)
		asp_trx_free_cmd(cmd);

	return -1;
}

//------------------------------------------------------------------------------
//
// Exported function
//
//------------------------------------------------------------------------------

/**
 * asp_prot_init_info - Initialize @asp_prot_info
 * @info: Pointer to the protocol info to be initialized.
 *
 * This function is used to initizlize a @asp_prot_info structure.
 *
 * Note that initialization is separated from the start function because the
 * start function can only be done when P2P connection in established.
 */
void asp_prot_init_info(struct asp_prot_info *info)
{
	os_memset(info, 0, sizeof(*info));
	dl_list_init(&info->bound_port_list);
	info->last_rx_seq = -1;
	info->magic = asp_prot_magic;
}

/**
 * asp_prot_deinit_info - De-initialize @asp_prot_info
 * @info: Pointer to the protocol info to be de-initialized.
 *
 * This function not only do the reverse of @asp_prot_init_info but also stop
 * all on going process.
  */
void asp_prot_deinit_info(struct asp_prot_info *info)
{
	struct asp_bound_port_entry *entry, *tmp;

	/* Stop all ongoing process */
	asp_prot_stop(info);

	info->magic = NULL;

	/* Free all bound ports */
	dl_list_for_each_safe(entry, tmp, &info->bound_port_list,
			      struct asp_bound_port_entry, list) {
		dl_list_del(&entry->list);
		os_free(entry);
	}
}

/**
 * asp_prot_close_session - Close ASP session if it is in opened state.
 * @info:
 * @reason:
 * Returns: 0 on success or -1 if failed
 *
 * This function bings the session state from opened to closed.
 */
int asp_prot_close_session(struct asp_prot_info *info, u8 reason)
{
	struct asp_session *entry = info->entry;

	if(!entry) {
		wpa_printf(MSG_INFO,
			   ASP_TAG
			   ": Session not yet initialized, skip\n");
		return 0;
	}

	/* Check state */
	if (entry->state != SESSION_OPENED) {
		wpa_printf(MSG_INFO,
			   ASP_TAG
			   ": Session is not in opened state, can't close\n");
		return -1;
	}

	/* Send rm session */
	if (asp_prot_send_rm_session(info, reason) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to rm session\n");
		return -1;
	}

	return 0;
}

/**
 * asp_prot_start - Start ASP coordination protocol process.
 * @info:
 * @trx:
 * @entry:
 * Returns: 0 on success or -1 if failed
 *
 * This function is used to start ASP coordination protocol process.
 */
int asp_prot_start(struct asp_prot_info *info, struct asp_trx_info *trx,
		   struct asp_session *entry)
{
	/* Check state */
	WPA_ASSERT(entry->state == SESSION_CLOSED);

	info->trx = trx;
	info->entry = entry;

	/* Send req session if pd_seeker */
	if (entry->p2ps_prov->pd_seeker) {
		if (asp_prot_send_req_session(info) < 0) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": Failed to req_session\n");
			goto fail;
		}
	}

	/* Mark as started */
	info->flag |= ASP_PROT_FLAG_STARTED;

	return 0;

fail:
	return -1;
}

/**
 * asp_prot_stop - Stop ASP coordination protocol process for the session.
 * @info: Pointer to the protocol info to stop.
 *
 * This function is used to gracefully stop an ASP coordination protocol
 * process.  All the dynamic members shall be freed and all the related
 * callback functions shall be notified of this stop event.
 * It shall be safe to call even when asp_prot_start() has not been called
 * at all.
 */
void asp_prot_stop(struct asp_prot_info *info)
{
	struct asp_trx_info *trx = info->trx;

	if (!(info->flag & ASP_PROT_FLAG_STARTED))
		return;

	/* Cancel all timeout related to @info */
	eloop_cancel_timeout(asp_prot_open_session_timeout, info, NULL);

	/* Cancel all outstanding cmds */
	asp_trx_cancel_cmd(trx, asp_prot_send_added_session_complete, info);
	asp_trx_cancel_cmd(trx, asp_prot_send_req_session_complete, info);
	asp_trx_cancel_cmd(trx, asp_prot_send_rm_session_complete, info);

	/* Mark as stopped */
	info->flag |= ASP_PROT_FLAG_STOPPED;
}

int asp_prot_bound_port(struct asp_prot_info *info, u16 port, u8 proto)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx;
	struct asp_bound_port_entry *bpe;

	if(!entry) {
		wpa_printf(MSG_INFO, ASP_TAG": Session not yet initialized\n");
		return 0;
	}

	ctx = entry->ctx;

	/* Check state */
	if (entry->state != SESSION_OPENED) {
		wpa_printf(MSG_INFO, ASP_TAG": Session is not in opened state, \
can't close\n");
		return -1;
	}

	/*
	 * TODO: If port access is not already allowed for the IP address
	 * associated with the ASP session, the ASP shall attempt to allow
	 * incoming connections on the indicated port and IP address.
	 */

	/* send allowed port */
	if (asp_prot_send_allowed_port(info, port, proto) < 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": Failed to send allowed port\n");
		return -1;
	}

	/* Add to the bound port list */
	bpe = (struct asp_bound_port_entry *)os_zalloc(sizeof(*bpe));
	if (bpe == NULL) {
		wpa_printf(MSG_ERROR,
			   "asp_prot_bound_port: Failed to allocate bound port\
entry");
		return -1;
	}
	os_memset(bpe, 0, sizeof(*bpe));
	dl_list_init(&bpe->list);
	bpe->port = port;
	bpe->proto = proto;
	bpe->ip = entry->ip_info.local_ip;
	os_snprintf(bpe->ip_txt, sizeof(bpe->ip_txt), "%s", inet_ntoa(bpe->ip));
	dl_list_add_tail(&info->bound_port_list, &bpe->list);

	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-PROT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " port=%u proto=%u ip=%s status=LocalPortAllowed",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id, port, proto, bpe->ip_txt);

	asp_prot_dump_bound_ports(info);

	return 0;
}

int asp_prot_release_port(struct asp_prot_info *info, u16 port, u8 proto)
{
	struct asp_session *entry = info->entry;
	struct asp_context *ctx;
	struct asp_bound_port_entry *bpe;
	int found = 0;

	if (!entry) {
		wpa_printf(MSG_INFO, ASP_TAG": Session not yet initialized\n");
		return 0;
	}

	ctx = entry->ctx;

	/*
	 * TODO: If the specified <ip_address, port, proto> is in not in use by
	 * any other open ASP session, incoming traffic is blocked from using
	 * the protocol proto to the port associated with the specified IP
	 * address.
	 */

	/* Remove from the bound port list */

	dl_list_for_each(bpe, &info->bound_port_list,
			 struct asp_bound_port_entry, list) {
		if (bpe->port == port && bpe->proto == proto) {
			found = 1;
			break;
		}
	}
	if (!found) {
		wpa_printf(MSG_ERROR, ASP_TAG ": specified <port, prot>=<%u, \
%u> pair not found", port, proto);
		return -1;
	}

	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-PROT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " port=%u proto=%u ip=%s status=LocalPortBlocked",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id, port, proto, bpe->ip_txt);

	dl_list_del(&bpe->list);
	os_free(bpe);

	asp_prot_dump_bound_ports(info);

	return 0;
}

void asp_prot_cmd_hdlr(struct asp_prot_info *info, u8 opcode, u8 seq,
		       const u8 *session_mac, u32 session_id,
		       struct wpabuf *payload)
{
	/* Check if duplicated */
	if (asp_prot_filter_dup_msg(info, opcode, seq))
		return;

	/* Dispatch cmd */
	switch (opcode) {
	default:
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Rx unknown cmd, opcode: %u\n", opcode);
		break;
	case ASP_MSG_REQUEST_SESSION:
		asp_prot_process_req_session(info, opcode, seq, session_mac,
					     session_id, payload);
		break;
	case ASP_MSG_ADDED_SESSION:
		asp_prot_process_added_session(info, opcode, seq, session_mac,
					       session_id, payload);
		break;
	case ASP_MSG_REMOVE_SESSION:
		asp_prot_process_rm_session(info, opcode, seq, session_mac,
					    session_id, payload);
		break;
	case ASP_MSG_ALLOWED_PORT:
		asp_prot_process_allowed_port(info, opcode, seq, session_mac,
					      session_id, payload);
		break;
	}
}
