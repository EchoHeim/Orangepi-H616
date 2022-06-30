//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//
#include "rt_asp_i.h"

//------------------------------------------------------------------------------
//
// Local functions
//
//------------------------------------------------------------------------------
static int asp_parse_check_payload_len(const struct wpabuf *payload,
				       size_t min_len)
{
	size_t payload_len;

	payload_len = wpabuf_len(payload);
	if (payload_len < min_len) {
		wpa_printf(MSG_ERROR, ASP_TAG": Invalid payload length: %d\n",
			   (int)payload_len);
		return -1;
	}

	return 0;
}

//------------------------------------------------------------------------------
//
// Global functions: parsing
//
//------------------------------------------------------------------------------
int asp_parse_req_session(struct wpabuf *payload, struct asp_msg *msg)
{
	os_memset(msg, 0, sizeof(*msg));

	/* Check payload length */
	if (asp_parse_check_payload_len(payload, 5) < 0)
		return -1;

	/* Adv ID */
	msg->adv_id = wpabuf_mhead_u8(payload) + 0;

	/* Session info len */
	msg->session_info_len = wpabuf_mhead_u8(payload) + 4;

	/* Session info */
	if (asp_parse_check_payload_len(payload,
					5 + *msg->session_info_len) < 0) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": invalid session info len: %d\n",
			   (int)*msg->session_info_len);
		return -1;
	}

	msg->session_info = wpabuf_mhead_u8(payload) + 5;

	return 0;
}

//------------------------------------------------------------------------------
//
// Global functions: extracting
//
//------------------------------------------------------------------------------
u32 asp_msg_get_adv_id(const struct asp_msg *msg)
{
	return msg->adv_id ? WPA_GET_BE32(msg->adv_id) : 0;
}

u8 asp_msg_get_session_info_len(const struct asp_msg *msg)
{
	return msg->session_info_len ? *msg->session_info_len : 0;
}

u8 * asp_msg_get_session_info(const struct asp_msg *msg)
{
	return msg->session_info ? msg->session_info : NULL;
}
