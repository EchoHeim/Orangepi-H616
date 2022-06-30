//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//
#ifndef	RT_ASP_H
#define	RT_ASP_H

#include "includes.h"

#include "common.h"
#include "utils/common.h"
#include "eloop.h"
#include "utils/list.h"
#include "rt_asp.h"
#include "p2p/p2p.h"
#include "wpa_supplicant_i.h"
#include "p2p_supplicant.h"

/* for waiting local IP addr */
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

#define ASP_TAG "ASP"

struct asp_pd_result {
	u8 status;
	u8 grp_mac[ETH_ALEN];
	u8 conncap;
	int passwd_id;
	unsigned int freq;
	u8 group_ssid[SSID_MAX_LEN];
	size_t group_ssid_len;
};

enum asp_session_state {
	/* Closed */
	INVALID = 0,
	INITIALIZED,

	/* PD */
	PD_STARTED,
	PD_REQUESTED,
	PD_DEFERRED,
	PD_COMPLETE,

	/* Group formation */
	GROUP_FORMATION_STARTED,
	GROUP_FORMATION_COMPLETE,
	GROUP_FORMATION_FAILED,

	/* IP addr */
	SELF_IP_WAITING,
	REMOTE_IP_WAITING,
	DHCP_COMPLETE,

	/* Coordination - common */
	SESSION_DEFERRED,
	SESSION_OPENED,		/* Session added */
	SESSION_CLOSED, 	/* Sock closed */
};

struct asp_p2p_grp_info {
	int go;
	u8 go_dev_addr[ETH_ALEN];
	char ssid_txt[33];
	int freq;
};

struct asp_ipv4_info {
	struct in_addr local_ip;
	struct in_addr remote_ip;
	int local_ip_attempts;
	int remote_ip_attempts;
};

struct asp_trx_cmd;
typedef void (*asp_trx_cmd_send_complete)(struct asp_trx_cmd *cmd, void *ctx);

enum asp_trx_cmd_send_result {
	ACKED = 0,
	NACKED,

	FAIL_AGED_OUT,
	FAIL_SEND_FAILED,
	FAIL_NO_RESP,

	INVALID_RES,
};

struct asp_trx_cmd {
	struct dl_list list;

#define ASP_TRX_CMD_FLAG_DEFERRED BIT(1)
#define ASP_TRX_CMD_FLAG_SENT BIT(2)
#define ASP_TRX_CMD_FLAG_RESP_RECVD BIT(3)
#define ASP_TRX_CMD_FLAG_RETRY BIT(4)

	u32 flag;

	struct wpabuf *buf;

	size_t attempts;
	u8 seq;

	struct in_addr dest;

	asp_trx_cmd_send_complete completion;

	void *completion_ctx;

	struct asp_trx_info *trx;

	enum asp_trx_cmd_send_result res;
        u8 nack_reason;
};

struct asp_bound_port_entry {
	struct dl_list list;
	u16 port;
	u8 proto;
	struct in_addr ip;
	char ip_txt[16];
};

struct asp_prot_info {
	const char *magic;
	int last_rx_seq; /* the last sequence number we processed */

#define ASP_PROT_FLAG_STARTED BIT(0)
#define ASP_PROT_FLAG_STOPPED BIT(1)
#define ASP_PROT_FLAG_REQ_SESSION_SENT BIT(4)
#define ASP_PROT_FLAG_ADDED_SESSION_RECVD BIT(5)
#define ASP_PROT_FLAG_REQ_SESSION_RECVD BIT(6)
#define ASP_PROT_FLAG_ADDED_SESSION_SENT BIT(7)
#define ASP_PROT_FLAG_SESSION_REMOVED BIT(8)
	u32 flag;

	struct asp_trx_info *trx;
	struct asp_session *entry;
	struct dl_list bound_port_list;
};

struct asp_session {
	struct dl_list list;
	const char *magic;
	struct asp_context *ctx;

#define ASP_SESSION_FLAG_STOP BIT(0)

	/**
	 * flag - One time event recording.
	 *
	 * Used to record events.  When a flag is set, it shall never be
	 * cleared.
	 */
	u32 flag;

	/* Time stamp */
	struct os_time ts;

	/* State */
	enum asp_session_state state;

	u8 peer_dev_addr[ETH_ALEN];

	int open_by_prov_disc;

	/**
	 * p2ps_prov - P2PS PD information
	 *
	 * For seeker, this is constructed based on the information given by
	 * the client.  For advertiser, this is given by asp_on_pd_complete()
	 * event callback.
	 * ASP will base on this information to construct @pd_result.
	 */
	struct p2ps_provision *p2ps_prov;

	/* PD result */
	struct asp_pd_result pd_result;

	/* Group info */
	struct asp_p2p_grp_info grp_info;

	/* IP addr */
	struct asp_ipv4_info ip_info;

	/* Coordination protocol */
	struct asp_prot_info prot_info;
};

struct asp_msg {
	u8 *adv_id;
	u8 *session_info_len;
	u8 *session_info;
};

//------------------------------------------------------------------------------
//
// rt_asp_dhcp
//
//------------------------------------------------------------------------------
struct in_addr asp_dhcp_parse_dhcpd_lease(const struct asp_context *ctx,
					  const u8 *mac);
struct in_addr asp_dhcp_parse_dhcpc_lease(const char * ifname);
void asp_dhcp_start(struct asp_context *ctx,
		    int go, const char *ssid_txt, int freq,
		    const char *psk_txt, const char *passphrase,
		    const u8 *go_dev_addr, int persistent,
		    const char *extra);
void asp_dhcp_stop(struct asp_context *ctx, int go,
		   const u8 *ssid, size_t ssid_len,
		   const char *reason);
//------------------------------------------------------------------------------
//
// rt_asp_trx
//
//------------------------------------------------------------------------------
int asp_trx_init(struct asp_trx_info *trx, struct in_addr local_ip,
		 struct in_addr remote_ip, asp_trx_cmd_hdlr cmd_hdlr,
		 void *cmd_ctx, asp_trx_init_complete init_complete_cb,
		 void *init_complete_ctx);
int asp_trx_started(struct asp_trx_info *trx);
void asp_trx_deinit(struct asp_trx_info *trx);
struct asp_trx_cmd * asp_trx_alloc_cmd(struct asp_trx_info *trx, size_t buflen,
				       asp_trx_cmd_send_complete completion,
				       void *completion_ctx);
void asp_trx_free_cmd(struct asp_trx_cmd *cmd);
int asp_trx_send_cmd(struct asp_trx_cmd *cmd, struct in_addr dest);
int asp_trx_cancel_cmd(struct asp_trx_info *trx,
		       asp_trx_cmd_send_complete completion,
		       void *completion_ctx);
int asp_trx_send_resp(struct asp_trx_info *trx, struct in_addr dest,
		      struct wpabuf *buf);
//------------------------------------------------------------------------------
//
// rt_asp_session
//
//------------------------------------------------------------------------------
const char * asp_session_state_txt(enum asp_session_state state);
void asp_set_session_state(struct asp_session *entry,
			   enum asp_session_state state);
os_time_t asp_get_session_state_time(const struct asp_session *entry);
struct asp_session * asp_get_session_entry(struct asp_context *ctx,
					   const u8 *session_mac,
					   u32 session_id);
struct asp_session *
asp_get_session_entry_by_state(struct asp_context *ctx,
			       enum asp_session_state state);
struct asp_session *
asp_get_session_entry_by_group(struct asp_context *ctx, int go,
			       const u8 *ssid, size_t ssid_len);
struct asp_session *
asp_get_session_entry_by_ptr(struct asp_context *ctx, void * ptr);
struct asp_session *
asp_get_session_entry_by_p2p_peer(struct asp_context *ctx, const u8 *peer);
int asp_check_stale_session_entry(const struct asp_session *entry);
struct asp_session * asp_add_session(struct asp_context *ctx,
				     const struct p2ps_provision *p2ps_prov);
int asp_del_session(struct asp_session *entry);
struct asp_session *
asp_clone_session(struct asp_context *ctx,
		  const struct asp_session *entry,
		  const struct p2ps_provision *p2ps_prov);
int asp_connect_p2p_session(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
			    const char *config_method,
			    struct p2ps_provision *p2ps_prov);
struct asp_session *
asp_connect_advertiser_asp_session(struct asp_context *ctx,
				   const u8 *session_mac,
				   u32 session_id,
				   struct wpabuf *req_sess_payload);
struct asp_session *
asp_connect_seeker_asp_session(struct asp_context *ctx, const u8 *adv_mac,
			       const struct p2ps_provision *p2ps_prov);
int asp_restart_asp_session(struct asp_session *entry);
void asp_dump_session(const struct asp_session *entry);
void asp_dump_session_list(struct asp_context *ctx);
//------------------------------------------------------------------------------
//
// asp_parse
//
//------------------------------------------------------------------------------
int asp_parse_req_session(struct wpabuf *payload, struct asp_msg *msg);
u32 asp_msg_get_adv_id(const struct asp_msg *msg);
u8 asp_msg_get_session_info_len(const struct asp_msg *msg);
u8 * asp_msg_get_session_info(const struct asp_msg *msg);

//------------------------------------------------------------------------------
//
// asp_prot
//
//------------------------------------------------------------------------------
void asp_prot_init_info(struct asp_prot_info *info);
void asp_prot_deinit_info(struct asp_prot_info *info);
int asp_prot_close_session(struct asp_prot_info *info, u8 reason);
int asp_prot_start(struct asp_prot_info *info, struct asp_trx_info *trx,
		   struct asp_session *entry);
void asp_prot_stop(struct asp_prot_info *info);
int asp_prot_bound_port(struct asp_prot_info *info, u16 port, u8 proto);
int asp_prot_release_port(struct asp_prot_info *info, u16 port, u8 proto);
void asp_prot_cmd_hdlr(struct asp_prot_info *info, u8 opcode, u8 seq,
		       const u8 *session_mac, u32 session_id,
		       struct wpabuf *payload);
#endif	/* RT_ASP_H */
