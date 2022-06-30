//------------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//------------------------------------------------------------------------------
//

#ifndef _RTK_P2P_ASP_API_H
#define	_RTK_P2P_ASP_API_H

#include "p2p/p2p.h"

#define ASP_VER ((u8)0x00)

/* ASP Coordination Protocol op code */
#define	ASP_MSG_REQUEST_SESSION 0
#define	ASP_MSG_ADDED_SESSION 1
#define	ASP_MSG_REJECTED_SESSION 2
#define	ASP_MSG_REMOVE_SESSION 3
#define	ASP_MSG_ALLOWED_PORT 4
#define	ASP_MSG_VERSION 5
#define	ASP_MSG_DEFERED_SESSION 6
#define	ASP_MSG_ACK 254
#define	ASP_MSG_NACK 255

enum asp_nack_reason
{
	ASP_NACK_REASON_INVALID_SESSION_MAC = 0x00,
	ASP_NACK_REASON_INVALID_SESSION_ID = 0x01,
	ASP_NACK_REASON_INVALID_OPCODE = 0x02,
	ASP_NACK_REASON_INVALID_SEQUENCE_NUM = 0x03,
	ASP_NACK_REASON_INVALID_NO_SESSION = 0x04,
	ASP_NACK_REASON_INVALID_UNKNOWN = 0x05,

	ASP_NACK_REASON_MAX = 0xFF
};

enum asp_rm_session_reason
{
	ASP_RM_SESSION_REASON_UNKNOWN = 0,
	ASP_RM_SESSION_REASON_REJECT_BY_USER = 1,
	ASP_RM_SESSION_REASON_ADV_SVC_UNAVAILABLE = 2,
	ASP_RM_SESSION_REASON_SYSTEM_FAILURE = 3,

	ASP_RM_SESSION_REASON_MAX = 0xFF
};

typedef void (*asp_trx_cmd_hdlr)(void *cmd_ctx, u8 opcode, u8 seq,
				 struct wpabuf *payload);
typedef void (*asp_trx_init_complete)(void *init_complete_ctx);

struct asp_trx_info {
	int sd;
	struct in_addr local_ip;
	struct in_addr remote_ip;

	asp_trx_init_complete init_complete_cb;
	void *init_complete_ctx;

	struct wpabuf *rxbuf;
	asp_trx_cmd_hdlr cmd_hdlr;
	void * cmd_ctx;

	struct asp_trx_cmd *cur_cmd;
	struct dl_list cmd_queue;

#define ASP_TRX_FLAG_INITIALIZED BIT(0)
#define ASP_TRX_FLAG_DEINITIALIZED BIT(1)
#define ASP_TRX_FLAG_INTERNAL_CMD BIT(2)
#define ASP_TRX_FLAG_VER_SENT BIT(4)
#define ASP_TRX_FLAG_VER_RECVD BIT(5)
#define ASP_TRX_FLAG_VER_DONE BIT(6)

	u32 flag;

	u8 next_seq; /* the nexe sequence number to use */
};

/**
 * struct asp_context - Main context for the ASP module.
 */
struct asp_context {

#define ASP_CONTEXT_FLAG_INITIALIZED BIT(0)

	/*
	 * flag - One time event recording.
	 */
	u32 flag;

	/**
	 * wpa_s - The corresponding wpa_supplicant instance.
	 */
	struct wpa_supplicant *wpa_s;

	u32 cur_search_id;
	u32 next_search_id;

	u32 next_session_id;

	u32 next_adv_id;

	/**
	 * session_list - The ASP session list
	 */
	struct dl_list session_list;

	struct asp_trx_info trx;
};

//------------------------------------------------------------------------------
//
// Init/deinit
//
//------------------------------------------------------------------------------
void asp_init(struct asp_context *ctx, struct wpa_supplicant *wpa_s);
void asp_deinit(struct asp_context *ctx);

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------
int asp_seek(struct wpa_supplicant *wpa_s,  u32 *search_id);
int asp_cancel_seek(struct wpa_supplicant *wpa_s, u32 search_id);
int asp_advertise(struct wpa_supplicant *wpa_s, int auto_accept,
		  const char *adv_str, u8 svc_state,
		  u16 config_methods, const char *svc_info,
		  const u8 *cpt_priority, u32 *adv_id);
int asp_cancel_advertise(struct wpa_supplicant *wpa_s, u32 adv_id);
int asp_connect_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
			const char *config_method,
			struct p2ps_provision *p2ps_prov,
			u32 *session_id);
int asp_remove_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
		       const u8 *session_mac, u32 session_id,
		       u8 reason);
int asp_bound_port(struct wpa_supplicant *wpa_s, const u8 *session_mac,
		   u32 session_id, u16 port, u8 proto);
int asp_release_port(struct wpa_supplicant *wpa_s, const u8 *session_mac,
		   u32 session_id, u16 port, u8 proto);

//------------------------------------------------------------------------------
//
// Event cb
//
//------------------------------------------------------------------------------
int asp_on_service_add_asp(struct asp_context *ctx,
			   int auto_accept, u32 adv_id,
			   const char *adv_str, u8 svc_state,
			   u16 config_methods, const char *svc_info,
			   const u8 *cpt_priority);
int asp_on_search_result(struct asp_context *ctx, const u8 *adv_mac,
			 const char *dev_name, u32 adv_id, const char *adv_str,
			 const struct wpabuf *p2ps_instance);
int asp_on_find_stopped(struct asp_context *ctx);
void asp_p2ps_prov_disc_req_rx(struct asp_context *ctx, const u8 *dev,
			       const char *dev_name,
			       int follow_on, u8 status,
			       const u8 *adv_mac, const u8 *ses_mac,
			       u32 adv_id, u32 ses_id, u8 conncap,
			       u8 auto_accept, const u8 *session_info,
			       size_t session_info_len);
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
		       const u8 *group_ssid, size_t group_ssid_len);
void asp_on_prov_disc_req_sent(struct asp_context *ctx, int success);
void asp_on_go_neg_req_sent(struct asp_context *ctx, int success);
void asp_on_go_neg_req_rx(struct asp_context *ctx, const u8 *src,
			  u16 dev_passwd_id, u8 go_intent);
void asp_on_go_neg_completed(struct asp_context *ctx,
			     struct p2p_go_neg_results *res);
void asp_on_p2p_group_started(struct asp_context *ctx,
			      int go, const char *ssid_txt, int freq,
			      const char *psk_txt, const char *passphrase,
			      const u8 *go_dev_addr, int persistent,
			      const char *extra);
void asp_on_group_formation_completed(struct asp_context *ctx, int success);
void asp_on_p2p_group_removed(struct asp_context *ctx, int go,
			      const u8 *ssid, size_t ssid_len,
			      const char *reason);
#endif // #ifndef _RTK_P2P_ASP_API_H
