//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//
#include "rt_asp_i.h"

static int asp_process_req_session(struct asp_context *ctx,
				   const u8 *session_mac, u32 session_id,
				   struct wpabuf *payload)
{
	struct asp_session *entry;

	entry = asp_connect_advertiser_asp_session(ctx, session_mac,
						   session_id, payload);
	if (entry == NULL)
	{
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to start session by req session\n");
		return -1;
	}

	if (entry->open_by_prov_disc == 0 && !entry->p2ps_prov->pd_seeker) {
		wpa_msg_global(ctx->wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=SessionRequestReceived",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);
	}

	return 0;
}

static void asp_rx_cmd(void *cmd_ctx, u8 opcode, u8 seq,
		       struct wpabuf *payload)
{
	struct asp_context *ctx = (struct asp_context *)cmd_ctx;
	struct asp_session *entry;
	const u8 *session_mac;
	u32 session_id;
	const u8 *payload_data2;
	struct wpabuf payload2;

	/* Get session_mac and session_id */
	if (wpabuf_len(payload) < 10)
		return;

	session_mac = wpabuf_head_u8(payload);
	session_id = WPA_GET_BE32(wpabuf_head_u8(payload) + ETH_ALEN);

	/* Strip session_mac and session_id */
	payload_data2 = (10 < wpabuf_len(payload)) ?
		wpabuf_head_u8(payload) + 10 : NULL;
	if (payload_data2 == NULL)
		wpabuf_set(&payload2, NULL, 0);
	else
		wpabuf_set(&payload2, payload_data2,
			   (size_t)(wpabuf_head_u8(payload) +
				    wpabuf_len(payload) - payload_data2));

	/* Get corresp. session */
	entry = asp_get_session_entry(ctx, session_mac, session_id);
	if (entry)
		asp_prot_cmd_hdlr(&entry->prot_info, opcode, seq, session_mac,
				  session_id, &payload2);
	else if (entry == NULL && opcode == ASP_MSG_REQUEST_SESSION)
		asp_process_req_session(ctx, session_mac, session_id, payload);
	else if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": no corresp. session for mac: "MACSTR", id %u\n",
			   MAC2STR(session_mac), session_id);
	}

	return;
}

static void asp_init_trx_complete(void *init_complete_ctx)
{
	struct asp_context *ctx = (struct asp_context *)init_complete_ctx;
	struct asp_trx_info *trx = &ctx->trx;
	struct asp_session *entry;

	if (!asp_trx_started(trx))
		goto fail;

	/* Start ASP protocol FORALL sessions */
	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		/* Check state */
		if (entry->state != DHCP_COMPLETE)
			continue;

		/* Set session state to closed */
		asp_set_session_state(entry, SESSION_CLOSED);

		/* Start to open session */
		if (asp_prot_start(&entry->prot_info, &ctx->trx, entry) < 0) {
			wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to open session for \
mac: "MACSTR", id %u\n",
				   MAC2STR(entry->p2ps_prov->session_mac),
				   entry->p2ps_prov->session_id);
			goto fail;
		}
	}

	return;

fail:
	asp_trx_deinit(&ctx->trx);
}

static void asp_wait_remote_ip_addr(void *eloop_data, void *user_ctx)
{
	struct asp_context *ctx = (struct asp_context *)eloop_data;
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	struct asp_session *entry;

	/* Get session entry */
	entry = asp_get_session_entry_by_state(ctx, REMOTE_IP_WAITING);
	if (entry ==  NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": no corresp. session entry in "
			   "remote ip waiting state\n");
		return;
	}

	entry->ip_info.remote_ip_attempts++;

	/* Get remote ip */
	entry->ip_info.remote_ip = entry->grp_info.go ?
		asp_dhcp_parse_dhcpd_lease(ctx,entry->p2ps_prov->pd_seeker ?
				      entry->p2ps_prov->adv_mac :
				      entry->p2ps_prov->session_mac) : /* TODO: shall be intf addr */
	asp_dhcp_parse_dhcpc_lease(wpa_s->ifname);

	wpa_printf(MSG_EXCESSIVE,
		   ASP_TAG": waiting for remote ip addr (attemps %u): %s\n",
		   entry->ip_info.remote_ip_attempts,
		   inet_ntoa(entry->ip_info.remote_ip));

	/* Check retry required */
	if (!entry->ip_info.remote_ip.s_addr &&
	    entry->ip_info.remote_ip_attempts < 32) {
		eloop_register_timeout(1, 0, asp_wait_remote_ip_addr, ctx, NULL);
		return;
	}

	if (!entry->ip_info.remote_ip.s_addr) {
		asp_set_session_state(entry, GROUP_FORMATION_FAILED);
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=GroupFormationFailed",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);
		return;
	}

	wpa_printf(MSG_INFO,
		   ASP_TAG": Got remote ip addr: %s, attempts: %d\n",
		   inet_ntoa(entry->ip_info.remote_ip),
		   entry->ip_info.remote_ip_attempts);

	asp_set_session_state(entry, DHCP_COMPLETE);

	/* Send group formation complete event */
	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-CONNECT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " status=GroupFormationComplete",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id);

	/* Start trx and asp protocol */
	if (asp_trx_init(&ctx->trx, entry->ip_info.local_ip,
			 entry->ip_info.remote_ip, asp_rx_cmd, ctx,
			 asp_init_trx_complete, ctx) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to start trx and ASP protocol\n");
		return;
	}
}

static void asp_wait_local_ip_addr(void *eloop_data, void *user_ctx)
{
	struct asp_context *ctx = (struct asp_context *)eloop_data;
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	int sd;
	struct ifreq ifr;
	struct asp_session *entry;
	unsigned char * p;

	entry = asp_get_session_entry_by_state(ctx, SELF_IP_WAITING);
	if (entry ==  NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": no corresp. session entry in "
			   "self ip waiting state\n");
		return;
	}

	entry->ip_info.local_ip_attempts++;

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, wpa_s->ifname, IFNAMSIZ-1);
	ioctl(sd, SIOCGIFADDR, &ifr);
	close(sd);

	entry->ip_info.local_ip =
		((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

	p = (unsigned char *)&entry->ip_info.local_ip.s_addr;

	wpa_printf(MSG_EXCESSIVE,
		   ASP_TAG": waiting for local ip addr (attemps %u): %s\n",
		   entry->ip_info.local_ip_attempts,
		   inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	if (192 == p[0] && 168 == p[1]) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": Got local ip addr: %s, attempts: %d\n",
			   inet_ntoa(entry->ip_info.local_ip),
			   entry->ip_info.local_ip_attempts);

		asp_set_session_state(entry, REMOTE_IP_WAITING);
		eloop_register_timeout(0, 100, asp_wait_remote_ip_addr, ctx, 0);
	}
	else if (entry->ip_info.local_ip_attempts < 32)
		eloop_register_timeout(1, 0, asp_wait_local_ip_addr, ctx, NULL);

	return;
}

static void asp_post_pd_process(void *eloop_data, void *user_ctx)
{
	struct asp_context *ctx = (struct asp_context *)eloop_data;
	struct asp_session *s = (struct asp_session *)user_ctx;
	struct asp_session *entry;
	struct asp_pd_result *pd_result;

	wpa_printf(MSG_INFO, ASP_TAG": post pd process\n");

	/* Get corresp. session entry */
	entry = asp_get_session_entry_by_ptr(ctx, s);
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": corresp. session entry not found\n");
		return;
	}

	pd_result = &entry->pd_result;

	if (pd_result->conncap == P2PS_SETUP_NEW &&
	    pd_result->passwd_id == DEV_PW_P2PS_DEFAULT) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": connect by init/auth go neg\n");
		if (wpas_p2p_connect(ctx->wpa_s,
				     entry->p2ps_prov->pd_seeker ?
				     entry->p2ps_prov->adv_mac :
				     entry->p2ps_prov->session_mac,
				     "12345670", WPS_P2PS,
				     0, // persistent_group
				     0, // auto_join
				     0, // join
				     entry->p2ps_prov->pd_seeker ? 0 : 1, // auth
				     -1, // go_intent, -1 for using default
				     pd_result->freq, // freq
				     0, // freq2
				     -1, // persistent_id
				     0, // pd
				     0, // start GO with 40 MHz
				     0, // start GO with VHT
				     0, // vht_chwidth
				     0, // he
				     pd_result->group_ssid, // group_ssid
				     pd_result->group_ssid_len // group_ssid_len
			    ) < 0) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": wpas_p2p_connect failed\n");
			return;
		}

		asp_set_session_state(entry, GROUP_FORMATION_STARTED);

		wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-CONNECT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " status=GroupFormationStarted",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id);
	}
}

//------------------------------------------------------------------------------
//
// Global functions
//
//------------------------------------------------------------------------------

void asp_init(struct asp_context *ctx, struct wpa_supplicant *wpa_s)
{
	os_memset(ctx, 0, sizeof(*ctx));

	ctx->wpa_s = wpa_s;
	dl_list_init(&ctx->session_list);
	ctx->cur_search_id = 0;
	ctx->next_search_id = 1;
	ctx->next_session_id = 1;
	ctx->next_adv_id = 1;
	ctx->flag |= ASP_CONTEXT_FLAG_INITIALIZED;

	wpa_printf(MSG_INFO, ASP_TAG": ASP initiated\n");
}

void asp_deinit(struct asp_context *ctx)
{
	struct asp_session *entry, *tmp;

	if (!(ctx->flag & ASP_CONTEXT_FLAG_INITIALIZED)) {
		return;
	}

	wpa_printf(MSG_INFO,
		   ASP_TAG": asp_deinit, sessions: %u\n",
		   dl_list_len(&ctx->session_list));

	/* Free all sessions */
	dl_list_for_each_safe(entry, tmp, &ctx->session_list, struct asp_session,
			      list) {
		asp_del_session(entry);
	}

	/* Deinitialize trx */
	asp_trx_deinit(&ctx->trx);

	wpa_printf(MSG_INFO, ASP_TAG": ASP de-initiated\n");
}

int asp_seek(struct wpa_supplicant *wpa_s,  u32 *search_id)
{
	struct asp_context *ctx = &wpa_s->asp;

	if (ctx->cur_search_id) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": Unable to seek because of a ongoing seek(%u)\n",
			   ctx->cur_search_id);
		return -1;
	}

	ctx->cur_search_id = ctx->next_search_id;
	*search_id = ctx->cur_search_id;
	ctx->next_search_id = ctx->next_search_id + 1 != 0 ?
		ctx->next_search_id + 1 :
		1;

	return 0;
}

int asp_cancel_seek(struct wpa_supplicant *wpa_s, u32 search_id)
{
	struct asp_context *ctx = &wpa_s->asp;

	if (ctx->cur_search_id == 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": No ongoing seek to cancel\n");
		return -1;
	}

	if (search_id != ctx->cur_search_id &&
		search_id != 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": Invalid search id %u, cur search id: %u\n",
			   search_id, ctx->cur_search_id);
		return -1;
	}

	wpas_p2p_stop_find(wpa_s);

	return 0;
}

int asp_advertise(struct wpa_supplicant *wpa_s, int auto_accept,
		  const char *adv_str, u8 svc_state,
		  u16 config_methods, const char *svc_info,
		  const u8 *cpt_priority, u32 *adv_id)
{
	struct asp_context *ctx = &wpa_s->asp;

	*adv_id = ctx->next_adv_id;
	ctx->next_adv_id = ctx->next_adv_id + 1 != 0 ?
		ctx->next_adv_id + 1 :
		1;

	wpa_printf(MSG_INFO, ASP_TAG": advertised: %s, auto_accept=%d \
adv_id=%u svc_state=%u config_methods=0x%04X svc_info=\'%s\'\n",
		   adv_str, auto_accept, *adv_id, svc_state,
		   config_methods, svc_info);

	if (wpas_p2p_listen(wpa_s, 0) < 0) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": Failed to put dev to listen state\n");
		return -1;
	}

	return wpas_p2p_service_add_asp(wpa_s, auto_accept, *adv_id, adv_str,
					(u8) svc_state, (u16) config_methods,
					svc_info, cpt_priority);
}

int asp_cancel_advertise(struct wpa_supplicant *wpa_s, u32 adv_id)
{

	if (wpas_p2p_service_del_asp(wpa_s, adv_id) < 0) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": Failed to delete asp service\n");
		return -1;
	}

	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-ADV-STATUS"
		       " adv_id=%u"
		       " status=%s",
		       adv_id, "NotAdvertised");

	return 0;
}

int asp_connect_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
			const char *config_method,
			struct p2ps_provision *p2ps_prov,
			u32 *session_id)
{
	struct asp_context *ctx = &wpa_s->asp;
	struct asp_session *entry;

	/* /\* Check if session already exists *\/ */
	/* entry = asp_get_session_entry(&wpa_s->asp, p2ps_prov->session_mac, */
	/* 			      p2ps_prov->session_id); */

	/* /\* Check if this is a stale entry (e.g., failed to connect), */
	/*  * remove it if it is *\/ */
	/* if (entry && asp_check_stale_session_entry(entry)) { */
	/* 	wpa_printf(MSG_INFO, ASP_TAG": Remove stale entry"); */
	/* 	asp_del_session(entry); */
	/* 	entry = NULL; */
	/* } */

	/* /\* Check if we only need to re-start ASP session w/o p2p connection *\/ */
	/* if (entry && entry->state == SESSION_CLOSED) */
	/* 	return asp_restart_asp_session(entry); */
	/* else if (entry) { */
	/* 	wpa_printf(MSG_ERROR, ASP_TAG": Session already exist"); */
	/* 	return -1; */
	/* } */

	/* Pick a session ID */
	p2ps_prov->session_id = ctx->next_session_id;
	ctx->next_session_id = ctx->next_session_id + 1 != 0 ?
		ctx->next_session_id + 1 :
		1;
	wpa_printf(MSG_INFO, ASP_TAG": session id: %u\n", p2ps_prov->session_id);

	/* Check if we are already L2 connected with peer */
	if (asp_get_session_entry_by_p2p_peer(ctx, peer_addr)) {
		entry = asp_connect_seeker_asp_session(ctx, peer_addr,
						       p2ps_prov);
		return entry ? 0 : -1;
	}

	/* No existing session with peer, start from p2p connection */
	if (asp_connect_p2p_session(wpa_s, peer_addr, config_method,
				    p2ps_prov) < 0) {
		wpa_printf(MSG_INFO,
			   ASP_TAG": Failed to connect p2p session\n");
		return -1;
	}

	*session_id = p2ps_prov->session_id;
	return 0;
}

int asp_remove_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
		       const u8 *session_mac, u32 session_id,
		       u8 reason)
{
	struct asp_context *ctx = &wpa_s->asp;
	struct asp_session *entry;
	int pd_seeker;
	int found = 0;
	const u8 *adv_mac;

	/* Determine if we are the seeker */
	pd_seeker = (os_memcmp(peer_addr, session_mac, ETH_ALEN) != 0);

	/* Get corresp. session entry */
	if (pd_seeker) {
		dl_list_for_each(entry, &ctx->session_list,
				 struct asp_session, list) {
			adv_mac = peer_addr;
			if (os_memcmp(adv_mac, entry->p2ps_prov->adv_mac,
				      ETH_ALEN) == 0 &&
			    session_id == entry->p2ps_prov->session_id &&
			    os_memcmp(session_mac,
				      entry->p2ps_prov->session_mac,
				      ETH_ALEN) == 0 &&
			    entry->state != INVALID) {
				found = 1;
				break;
			}
		}
	} else {
		dl_list_for_each(entry, &ctx->session_list,
				 struct asp_session, list) {
			if (os_memcmp(session_mac, entry->p2ps_prov->session_mac,
				      ETH_ALEN) == 0 &&
			    session_id == entry->p2ps_prov->session_id &&
			    entry->state != INVALID) {
				found = 1;
				break;
			}
		}
	}

	if (found == 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": can't find session to remove\n");
		return -1;
	}

	return asp_prot_close_session(&entry->prot_info, reason);
}

int asp_bound_port(struct wpa_supplicant *wpa_s, const u8 *session_mac,
		   u32 session_id, u16 port, u8 proto)
{	struct asp_context *ctx = &wpa_s->asp;
	struct asp_session *entry;
	int found = 0;

	wpa_printf(MSG_INFO, ASP_TAG": bound port: %u, session_mac: "MACSTR",\
session_id: 0x%x, proto: %u\n",
		   port, MAC2STR(session_mac), session_id, proto);

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		if (os_memcmp(session_mac, entry->p2ps_prov->session_mac,
			      ETH_ALEN) == 0 &&
		    session_id == entry->p2ps_prov->session_id &&
		    entry->state == SESSION_OPENED) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": can't find session notify bound\
port\n");
		return -1;
	}

	return asp_prot_bound_port(&entry->prot_info, port, proto);
}

int asp_release_port(struct wpa_supplicant *wpa_s, const u8 *session_mac,
		     u32 session_id, u16 port, u8 proto)
{	struct asp_context *ctx = &wpa_s->asp;
	struct asp_session *entry;
	int found = 0;

	wpa_printf(MSG_INFO, ASP_TAG": release port: %u, session_mac: "MACSTR",\
session_id: 0x%x, proto: %u\n",
		   port, MAC2STR(session_mac), session_id, proto);

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		if (os_memcmp(session_mac, entry->p2ps_prov->session_mac,
			      ETH_ALEN) == 0 &&
		    session_id == entry->p2ps_prov->session_id) {
			found = 1;
			break;
		}
	}

	if (found == 0) {
		wpa_printf(MSG_ERROR, ASP_TAG": can't find session notify\
release port\n");
		return -1;
	}

	return asp_prot_release_port(&entry->prot_info, port, proto);
}

int asp_on_service_add_asp(struct asp_context *ctx,
			   int auto_accept, u32 adv_id,
			   const char *adv_str, u8 svc_state,
			   u16 config_methods, const char *svc_info,
			   const u8 *cpt_priority)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;

	/* TODO: Send event only when service status is changed.
	 * We now send this event whenevr service is added or replaced,
	 * regardless whether the it is actually a status change */

	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-ADV-STATUS"
		       " adv_id=%u"
		       " status=%s",
		       adv_id,
		       svc_state == 1 ? "Advertised" : "NotAdvertised");

	return 0;
}

int asp_on_search_result(struct asp_context *ctx, const u8 *adv_mac,
			 const char *dev_name, u32 adv_id, const char *adv_str,
			 const struct wpabuf *p2ps_instance)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;

	if (ctx->cur_search_id == 0)
		return 0;

	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-SEARCH-RESULT"
		       " search_id=%u"
		       " service_mac=" MACSTR
		       " service_device_name='%s'"
		       " adv_id=%u"
		       " service_name='%s'",
		       ctx->cur_search_id, MAC2STR(adv_mac), dev_name,
		       adv_id, adv_str);

	return 0;
}

int asp_on_find_stopped(struct asp_context *ctx)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;

	if (ctx->cur_search_id == 0)
		return 0;

	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-SEARCH-TERMINATED %u", ctx->cur_search_id);

	ctx->cur_search_id = 0;

	return 0;
}

void asp_p2ps_prov_disc_req_rx(struct asp_context *ctx, const u8 *dev,
			       const char *dev_name,
			       int follow_on, u8 status,
			       const u8 *adv_mac, const u8 *ses_mac,
			       u32 adv_id, u32 ses_id, u8 conncap,
			       u8 auto_accept, const u8 *session_info,
			       size_t session_info_len)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;

	if (session_info_len)
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-SESSION-REQ"
			       " adv_id=%u"
			       " session_mac=" MACSTR
			       " service_device_name='%s'"
			       " session_id=%u"
			       " session_info=\'%.*s\'",
			       adv_id, MAC2STR(ses_mac), dev_name,
			       ses_id, (int) session_info_len, session_info);
	else
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-SESSION-REQ"
			       " adv_id=%u"
			       " session_mac=" MACSTR
			       " service_device_name='%s'"
			       " session_id=%u"
			       " session_info=\'\'",
			       adv_id, MAC2STR(ses_mac), dev_name, ses_id);

	wpa_msg_global(wpa_s, MSG_INFO,
		       "ASP-CONNECT-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " status=SessionRequestReceived",
		       MAC2STR(ses_mac), ses_id);

	return;
}

int asp_on_pd_complete(struct asp_context *ctx,
		       const struct  p2ps_provision *p2ps_prov,
		       u8 status, const u8 *dev,
		       const u8 *adv_mac, const u8 *ses_mac,
		       const u8 *grp_mac, u32 adv_id, u32 ses_id,
		       u8 conncap, int passwd_id,
		       const u8 *persist_ssid,
		       size_t persist_ssid_size, int response_done,
		       int prov_start, const char *session_info,
		       const u8 *feat_cap, size_t feat_cap_len,
		       unsigned int freq,
		       const u8 *group_ssid, size_t group_ssid_len)
{
	struct asp_session *entry;

	/* Get corresp. session entry */
	if (p2ps_prov->pd_seeker) {
		entry = asp_get_session_entry(ctx, ses_mac, ses_id);
		if (entry == NULL) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": no corresp session entry\n");
			return -1;
		}
	} else {
		/* Remove stale entry, e.g., rx multiple PD at once */
		while ((entry = asp_get_session_entry(ctx, ses_mac,
						      ses_id)) != NULL)
			asp_del_session(entry);

		entry = asp_add_session(ctx, p2ps_prov);
		if (entry  == NULL) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": Failed to add session\n");
			return -1;
		}

		asp_set_session_state(entry, PD_REQUESTED);
		entry->open_by_prov_disc = 1;
	}

	/* Update session entry state */
	asp_set_session_state(entry, PD_COMPLETE);

	if (p2ps_prov->pd_seeker && entry->open_by_prov_disc)
		wpa_msg_global(ctx->wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=SessionRequestAccepted",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);

	/* Record PD rsult */
	entry->pd_result.status = status;
	os_memcpy(entry->pd_result.grp_mac, grp_mac, ETH_ALEN);
	entry->pd_result.conncap = conncap;
	entry->pd_result.passwd_id = passwd_id;
	entry->pd_result.freq = freq;
	os_memcpy(entry->pd_result.group_ssid, group_ssid, group_ssid_len);
	entry->pd_result.group_ssid_len = group_ssid_len;

	/* Start post PD process */
	if (passwd_id == DEV_PW_P2PS_DEFAULT)
		eloop_register_timeout(0, 0, asp_post_pd_process, ctx, entry);

	return 0;
}

void asp_on_prov_disc_req_sent(struct asp_context *ctx, int success)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	struct asp_session * entry;

	entry = asp_get_session_entry_by_state(ctx, PD_STARTED);
	if (entry == NULL)
		return;

	if (success)
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=SessionRequestSent",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);

	return;
}

void asp_on_go_neg_req_sent(struct asp_context *ctx, int success)
{
	struct asp_session * entry;

	entry = asp_get_session_entry_by_state(ctx, GROUP_FORMATION_STARTED);
	if (entry == NULL)
		return;

	return;
}

void asp_on_go_neg_req_rx(struct asp_context *ctx, const u8 *src,
			  u16 dev_passwd_id, u8 go_intent)
{
	struct asp_session * entry;

	entry = asp_get_session_entry_by_state(ctx, GROUP_FORMATION_STARTED);
	if (entry == NULL)
		return;

	return;
}

void asp_on_go_neg_completed(struct asp_context *ctx,
			     struct p2p_go_neg_results *res)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	struct asp_session * entry;

	entry = asp_get_session_entry_by_state(ctx, GROUP_FORMATION_STARTED);
	if (entry == NULL)
		return;

	if (res->status != 0) {
		asp_set_session_state(entry, GROUP_FORMATION_FAILED);
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=GroupFormationFailed",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);
	}

	return;
}

void asp_on_p2p_group_started(struct asp_context *ctx,
			      int go, const char *ssid_txt, int freq,
			      const char *psk_txt, const char *passphrase,
			      const u8 *go_dev_addr, int persistent,
			      const char *extra)
{
	struct asp_session *entry;

	/* Find asp session entry */
	entry = asp_get_session_entry_by_state(ctx, GROUP_FORMATION_STARTED);
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   "asp_on_p2p_group_started: no corresp. entry");
		return;
	}

	/* Record grp info */
	entry->grp_info.go = go;
	os_memcpy(entry->grp_info.go_dev_addr, go_dev_addr, ETH_ALEN);
	os_memcpy(entry->grp_info.ssid_txt, ssid_txt, os_strlen(ssid_txt) + 1);
	entry->grp_info.freq = freq;

	asp_set_session_state(entry, GROUP_FORMATION_COMPLETE);

	/* Start dhcpc/dhcpd */
	asp_dhcp_start(ctx, go, ssid_txt, freq, psk_txt, passphrase, go_dev_addr,
		       persistent, extra);

	/* wait for dhcp done */
	asp_set_session_state(entry, SELF_IP_WAITING);
	eloop_register_timeout(0, 100, asp_wait_local_ip_addr, ctx, 0);
}

void asp_on_group_formation_completed(struct asp_context *ctx, int success)
{
	struct asp_session *entry;
	struct wpa_supplicant *wpa_s = ctx->wpa_s;

	/* Find asp session entry */
	entry = asp_get_session_entry_by_state(ctx, GROUP_FORMATION_STARTED);
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": asp_on_group_formation_completed: no corresp. \
entry");
		return;
	}

	if (!success) {
		asp_set_session_state(entry, GROUP_FORMATION_FAILED);
		wpa_msg_global(wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=GroupFormationFailed",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);
	}
}

void asp_on_p2p_group_removed(struct asp_context *ctx, int go,
			      const u8 *ssid, size_t ssid_len,
			      const char *reason)
{
	struct asp_session *entry, *tmp;

	/* Find asp session entry */
	/* TODO: there may be multiple sessions using the group */
	entry = asp_get_session_entry_by_group(ctx, go, ssid, ssid_len);
	if (entry == NULL ||
	    entry->state <= GROUP_FORMATION_COMPLETE) {
		wpa_printf(MSG_ERROR,
			   "asp_on_p2p_group_removed: no corresp. entry");
		return;
	}

	dl_list_for_each_safe(entry, tmp, &ctx->session_list,
			      struct asp_session, list) {
		/* Match p2p group */
		if (ssid_len != os_strlen(entry->grp_info.ssid_txt) ||
		    os_memcmp(ssid, entry->grp_info.ssid_txt, ssid_len) != 0)
			continue;

		/* Send related events */
		if (entry->state == SESSION_OPENED)
			wpa_msg_global(ctx->wpa_s, MSG_INFO,
				       "ASP-SESSION-STATUS"
				       " session_mac=" MACSTR
				       " session_id=%u"
				       " state=Closed"
				       " status=TODO",
				       MAC2STR(entry->p2ps_prov->session_mac),
				       entry->p2ps_prov->session_id);

		wpa_msg_global(ctx->wpa_s, MSG_INFO,
			       "ASP-CONNECT-STATUS"
			       " session_mac=" MACSTR
			       " session_id=%u"
			       " status=Disconnected",
			       MAC2STR(entry->p2ps_prov->session_mac),
			       entry->p2ps_prov->session_id);

		/* Remove session */
		asp_del_session(entry);
	}

	/* Stop dhcp */
	asp_dhcp_stop(ctx, go, ssid, ssid_len, reason);

	/* Deinitialize trx */
	asp_trx_deinit(&ctx->trx);
}
