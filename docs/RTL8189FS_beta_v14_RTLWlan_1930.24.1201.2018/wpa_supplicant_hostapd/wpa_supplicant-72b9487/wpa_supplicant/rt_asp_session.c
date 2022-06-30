//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//

#include "rt_asp_i.h"

const char * asp_magic = "RT_ASP";

//------------------------------------------------------------------------------
//
// Local functions
//
//------------------------------------------------------------------------------

static void asp_init_session_entry(struct asp_session *entry)
{
	os_memset(entry, 0, sizeof(*entry));

	dl_list_init(&entry->list);
	asp_prot_init_info(&entry->prot_info);
	os_get_time(&entry->ts);
	asp_set_session_state(entry, INITIALIZED);
	entry->open_by_prov_disc = -1;
	entry->state = INITIALIZED;
	entry->magic = asp_magic;
}

/**
 * asp_deinit_session_entry - De-initialize a session entry.
 * @entry: Pointer to session entry to be de-initialized
 *
 * This function is used to de-initialize a session entry.  All the dynamic
 * members shall be gracefully freed.
 */
static void asp_deinit_session_entry(struct asp_session *entry)
{
	/* ASP coordination protocol */
	asp_prot_deinit_info(&entry->prot_info);

	/* p2ps PD info */
	if (entry->p2ps_prov)
		os_free(entry->p2ps_prov);

	/* Reset session state */
	asp_set_session_state(entry, INVALID);
}

static struct p2ps_provision *
asp_make_p2ps_prov_by_req_session(const struct wpa_supplicant *wpa_s,
				  const u8 *session_mac, u32 session_id,
				  struct wpabuf *payload)
{
	struct asp_msg msg;
	struct p2ps_provision *p2ps_prov;

	if (asp_parse_req_session(payload, &msg) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to parse req session\n");
		goto fail;
	}

	/* Make p2ps_prov */
	p2ps_prov = os_zalloc(sizeof(*p2ps_prov) + *msg.session_info_len + 1);
	if (p2ps_prov == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": Failed to allocate memory for p2ps_prov\n");
		goto fail;
	}

	p2ps_prov->adv_id = *msg.adv_id;
	p2ps_prov->session_id = session_id;
	os_memcpy(p2ps_prov->session_mac, session_mac, ETH_ALEN);
	os_memcpy(p2ps_prov->adv_mac, wpa_s->own_addr, ETH_ALEN);
	os_memcpy(p2ps_prov->info, msg.session_info, *msg.session_info_len);
	p2ps_prov->info[*msg.session_info_len] = 0;

	return p2ps_prov;

fail:
	if (p2ps_prov)
		os_free(p2ps_prov);

	return NULL;
}

static struct asp_session *
asp_connect_asp_session(struct asp_context *ctx,
			const u8 *peer_dev_addr,
			const struct p2ps_provision *p2ps_prov)
{
	struct asp_session *entry, *clone = NULL;

	entry = asp_get_session_entry_by_p2p_peer(ctx, peer_dev_addr);
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
		       ASP_TAG": No connected session\n");
		goto fail;
	}

	clone = asp_clone_session(ctx, entry, p2ps_prov);
	if (clone == NULL) {
	    wpa_printf(MSG_ERROR,
		       ASP_TAG": Failed to clone session for a connected peer dev\n");
	    goto fail;
	}

	clone->open_by_prov_disc = 0;
	asp_set_session_state(clone, SESSION_CLOSED);

	if (asp_prot_start(&clone->prot_info, &ctx->trx, clone) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG
			   ": Failed to start prot for the cloned session\n");
	    goto fail;
	}

	return clone;

fail:
	if (clone)
		asp_del_session(clone);

	return NULL;
}

//------------------------------------------------------------------------------
//
// Exported functions
//
//------------------------------------------------------------------------------

const char * asp_session_state_txt(enum asp_session_state state)
{
	switch (state) {
	case INVALID:
		return "INVALID";
	case INITIALIZED:
		return "INITIALIZED";
	case PD_STARTED:
		return "PD_STARTED";
	case PD_REQUESTED:
		return "PD_REQUESTED";
	case PD_DEFERRED:
		return "PD_DEFERRED";
	case PD_COMPLETE:
		return "PD_COMPLETE";
	case GROUP_FORMATION_STARTED:
		return "GROUP_FORMATION_STARTED";
	case GROUP_FORMATION_COMPLETE:
		return "GROUP_FORMATION_COMPLETE";
	case GROUP_FORMATION_FAILED:
		return "GROUP_FORMATION_FAILED";
	case SELF_IP_WAITING:
		return "SELF_IP_WAITING";
	case REMOTE_IP_WAITING:
		return "REMOTE_IP_WAITING";
	case DHCP_COMPLETE:
		return "DHCP_COMPLETE";
	case SESSION_DEFERRED:
		return "SESSION_DEFERRED";
	case SESSION_OPENED:
		return "SESSION_OPENED";
	case SESSION_CLOSED:
		return "SESSION_CLOSED";
	default:
		return "?";
	}
}

void asp_set_session_state(struct asp_session *entry,
			   enum asp_session_state state)
{
	wpa_printf(MSG_INFO,
		   ASP_TAG": change session %d state from [%s] to [%s], duration: %u\n",
		   (entry->p2ps_prov) ? entry->p2ps_prov->session_id : -1,
		   asp_session_state_txt(entry->state),
		   asp_session_state_txt(state),
		   (int)asp_get_session_state_time(entry));

	entry->state = state;
	os_get_time(&entry->ts);

	if (state == SESSION_OPENED ||
	    state == SESSION_CLOSED)
		asp_dump_session_list(entry->ctx);
}

os_time_t asp_get_session_state_time(const struct asp_session *entry)
{
	struct os_time now;

	os_get_time(&now);
	return now.sec - entry->ts.sec;
}

struct asp_session * asp_get_session_entry(struct asp_context *ctx,
					   const u8 *session_mac,
					   u32 session_id)
{
	struct asp_session *entry;

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
	        if (os_memcmp(session_mac,
			      entry->p2ps_prov->session_mac, ETH_ALEN) == 0 &&
		    session_id == entry->p2ps_prov->session_id &&
		    entry->state != INVALID) {
			return entry;
		}
	}

	return NULL;
}

struct asp_session *
asp_get_session_entry_by_state(struct asp_context *ctx,
			       enum asp_session_state state)
{
	struct asp_session *entry;

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
	        if (entry->state == state)
			return entry;
	}

	return NULL;
}

struct asp_session *
asp_get_session_entry_by_group(struct asp_context *ctx, int go,
			       const u8 *ssid, size_t ssid_len)
{
	struct asp_session *entry;

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		if (go == entry->grp_info.go &&
		    ssid_len == os_strlen(entry->grp_info.ssid_txt) &&
		    os_memcmp(ssid, entry->grp_info.ssid_txt, ssid_len) == 0)
			return entry;
	}

	return NULL;
}

struct asp_session *
asp_get_session_entry_by_ptr(struct asp_context *ctx, void * ptr)
{
	struct asp_session *entry;

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		if ((void *)entry == ptr)
			return entry;
	}

	return NULL;
}

struct asp_session *
asp_get_session_entry_by_p2p_peer(struct asp_context *ctx, const u8 *peer)
{
	struct asp_session *entry;
	int found = 0;

	dl_list_for_each(entry, &ctx->session_list,
			 struct asp_session, list) {
		if (DHCP_COMPLETE < entry->state &&
		    os_memcmp(peer, entry->peer_dev_addr,
			      sizeof(entry->peer_dev_addr)) == 0) {
			found = 1;
			break;
		}
	}

	return found ? entry : NULL;

}

int asp_check_stale_session_entry(const struct asp_session *entry)
{
	os_time_t elapse;

	elapse = asp_get_session_state_time(entry);

	wpa_printf(MSG_INFO,
		   ASP_TAG": Try to remove stale entry in state %s \
for %d sec\n",
		   asp_session_state_txt(entry->state), (int)elapse);

	if (entry->state < PD_COMPLETE &&
	    10 < elapse) {
		return 1;
	}

	if (entry->state < GROUP_FORMATION_COMPLETE &&
	    10 < elapse) {
		return 1;
	}

	if (entry->state < SELF_IP_WAITING  &&
	    60 < elapse) {
		return 1;
	}

	if (entry->state < DHCP_COMPLETE &&
	    30 < elapse) {
		return 1;
	}

	wpa_printf(MSG_INFO, ASP_TAG": Entry is not stale\n");

	return 0;
}

struct asp_session * asp_add_session(struct asp_context *ctx,
				     const struct p2ps_provision *p2ps_prov)
{
	struct asp_session *entry;
	size_t info_len = 0;

	/* Allocate a session entry */
	entry = (struct asp_session *)os_zalloc(sizeof(*entry));
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   "asp_add_session: Failed to allocate asp session");
		return NULL;
	}

	asp_init_session_entry(entry);

	/* Duplicate the p2ps_prov struct */
	info_len = os_strlen(p2ps_prov->info);
	entry->p2ps_prov =
		(struct p2ps_provision *)os_zalloc(sizeof(*p2ps_prov) +
						   info_len +1);
	if (entry->p2ps_prov == NULL) {
		wpa_printf(MSG_ERROR,
			   "asp_add_session: Failed to allocate p2ps_prov");
		return NULL;
	}

	os_memcpy(entry->p2ps_prov, p2ps_prov, sizeof(*p2ps_prov));
	os_memcpy(entry->p2ps_prov->info, p2ps_prov->info, info_len + 1);

	entry->ctx = ctx;

	os_memcpy(entry->peer_dev_addr,
		  p2ps_prov->pd_seeker ? p2ps_prov->adv_mac :
		  p2ps_prov->session_mac, ETH_ALEN);

	/* Add to session list */
	dl_list_add_tail(&ctx->session_list, &entry->list);

	wpa_printf(MSG_INFO, "asp_add_session: session added");

	/* Send Session Status event */
	wpa_msg_global(ctx->wpa_s, MSG_INFO,
		       "ASP-SESSION-STATUS"
		       " session_mac=" MACSTR
		       " session_id=%u"
		       " state=%s"
		       " status=OK",
		       MAC2STR(entry->p2ps_prov->session_mac),
		       entry->p2ps_prov->session_id,
		       p2ps_prov->pd_seeker ? "Initiated" : "Requested");

	return entry;
}

/**
 * asp_del_session - Delete a session from the session list
 * @entry: Pointer to session entry in the session list
 * Returns: 0 on success or -1 if failed
 *
 * This function is used to gracefully remove a session entry from the
 * session list.  It frees the session entry by calling
 * asp_deinit_session_entry.
 */
int asp_del_session(struct asp_session *entry)
{
	wpa_printf(MSG_INFO, "Remove entry in state %s for %d sec\n",
		   asp_session_state_txt(entry->state),
		   (int)asp_get_session_state_time(entry));

	asp_deinit_session_entry(entry);
	dl_list_del(&entry->list);
	os_free(entry);
	return 0;
}

struct asp_session *
asp_clone_session(struct asp_context *ctx,
		  const struct asp_session *entry,
		  const struct p2ps_provision *p2ps_prov)
{
	struct asp_session *clone;

	/* Clone the entry and p2ps_prov by adding the session */
	clone = asp_add_session(ctx, p2ps_prov);

	/* Skip pd_result as it is not used at all for an already connected
	 * session */

	/* Copy grp_info */
	os_memcpy(&clone->grp_info, &entry->grp_info, sizeof(entry->grp_info));

	/* Copy ip_info */
	os_memcpy(&clone->ip_info, &entry->ip_info, sizeof(entry->ip_info));

	/* Skip prot_info as it is initialized in @asp_add_session and the
	 * prot shall be started later by calling @asp_prot_start */

	return clone;
}

int asp_connect_p2p_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
			    const char *config_method,
			    struct p2ps_provision *p2ps_prov)
{
	struct asp_context *ctx = &wpa_s->asp;
	struct asp_session *entry;

	/* Start PD */
	if (wpas_p2p_prov_disc(wpa_s, p2ps_prov->adv_mac, NULL,
			       WPAS_P2P_PD_FOR_ASP,
			       p2ps_prov) < 0) {
		wpa_printf(MSG_ERROR,
			   "asp_connect_p2p_session: failed to send PD req\n");
		return -1;
	}

	/* Add asp session when PD started successfully*/
	entry = asp_add_session(ctx, p2ps_prov);
	if (entry == NULL) {
		/* TODO: stop sending PD req */
		return -1;
	}

	asp_set_session_state(entry, PD_STARTED);

	entry->open_by_prov_disc = 1;

	return 0;
}


struct asp_session *
asp_connect_advertiser_asp_session(struct asp_context *ctx,
				   const u8 *session_mac,
				   u32 session_id,
				   struct wpabuf *req_sess_payload)
{
	struct p2ps_provision *p2ps_prov;
	struct asp_session *entry;

	p2ps_prov =
		asp_make_p2ps_prov_by_req_session(ctx->wpa_s, session_mac,
						    session_id, req_sess_payload);
	if (p2ps_prov == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to make p2ps_prov\n");
		goto fail;
	}

	entry = asp_connect_asp_session(ctx, session_mac, p2ps_prov);
	if (entry == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to start session\n");
		os_free(p2ps_prov);
		goto fail;
	}

	return entry;

fail:
	if (p2ps_prov)
		os_free(p2ps_prov);
	return NULL;
}

struct asp_session *
asp_connect_seeker_asp_session(struct asp_context *ctx, const u8 *adv_mac,
			       const struct p2ps_provision *p2ps_prov)
{
	return asp_connect_asp_session(ctx, adv_mac, p2ps_prov);
}

int asp_restart_asp_session(struct asp_session *entry)
{
	struct asp_context *ctx;

	if (!entry)
		return -1;

	if (entry->state != SESSION_CLOSED)
		return -1;

	wpa_printf(MSG_INFO,
		   ASP_TAG": Re-starting ASP session");

	ctx = entry->ctx;

	entry->prot_info.flag = 0;
	entry->open_by_prov_disc = 0;

	/* Start ASP protocol */
	if (asp_prot_start(&entry->prot_info, &ctx->trx, entry) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to re-start ASP session");
		return -1;
	}

	return 0;
}

void asp_dump_session(const struct asp_session *entry)
{
	wpa_printf(MSG_INFO, ASP_TAG": (ses_mac, ses_id): ("MACSTR", %u)\n",
		   MAC2STR(entry->p2ps_prov->session_mac),
		   entry->p2ps_prov->session_id);
	wpa_printf(MSG_INFO, ASP_TAG": (adv_mac, adv_id): ("MACSTR", %u)\n",
		   MAC2STR(entry->p2ps_prov->adv_mac),
		   entry->p2ps_prov->adv_id);
	wpa_printf(MSG_INFO, ASP_TAG": state: %s\n",
		   asp_session_state_txt(entry->state));
	wpa_printf(MSG_INFO, ASP_TAG": PD: %d\n", entry->open_by_prov_disc);
	wpa_printf(MSG_INFO, ASP_TAG": rel-time: %d\n",
		   (int)asp_get_session_state_time(entry));
}

void asp_dump_session_list(struct asp_context *ctx)
{
	int cnt = 0;
	struct asp_session *entry;

	dl_list_for_each(entry,&ctx->session_list,
			 struct asp_session, list) {
		wpa_printf(MSG_INFO, ASP_TAG": --- Session #%d ---\n", cnt++);
		asp_dump_session(entry);
	}
}
