//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//
#include "includes.h"

#ifdef CONFIG_CTRL_IFACE

#ifdef CONFIG_CTRL_IFACE_UNIX
#include <dirent.h>
#endif /* CONFIG_CTRL_IFACE_UNIX */

#include "common/wpa_ctrl.h"
#include "utils/common.h"
#include "utils/eloop.h"

static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *mon_conn;
static int wpa_cli_attached = 0;

#ifndef CONFIG_CTRL_IFACE_DIR
#define CONFIG_CTRL_IFACE_DIR "/var/run/wpa_supplicant"
#endif /* CONFIG_CTRL_IFACE_DIR */
static const char *action_file = "./examples/asp-action.sh";
static const char *ctrl_iface_dir = CONFIG_CTRL_IFACE_DIR;
static const char *client_socket_dir = NULL;
static char *ctrl_ifname = NULL;
static int ping_interval = 5;
static int interactive = 0;
static char *ifname_prefix = NULL;

char *optarg;

static char cmd_buf[4096];

static char *dev_name = NULL;
static char *role = NULL;

const char *service_name = "org.wi-fi.wfds.send.rx";
const char *service_info_prefix = "ssh";
static char service_info_str[1024]; /* For advertiser */
static const char *pub_key_file = "./wfds-send.pub";
static const char *priv_key_file = "./wfds-send";
static char authorized_key_file[128];
static char *user = NULL;
static u32 search_id;
static u8 session_mac[ETH_ALEN];
static u32 session_id;
static u8 adv_mac[ETH_ALEN];
static u32 adv_id;
static char service_info[1024]; 	/* For seeker */

static char *file_name;
static char *file_type;
static char session_info[1024];	/* Used by both seeker and advertiser */

static int serv_asp_resp_processed;

static void wpa_cli_ping(void *eloop_ctx, void *timeout_ctx);
static void wpa_cli_reconnect(void);
static void wpa_cli_close_connection(void);

static void on_seek_service_return(const char *buf);
static void on_connect_session_return(const char *buf);

//------------------------------------------------------------------------------
//
// Local functions: utility
//
//------------------------------------------------------------------------------
/* static int wpa_cli_exec(const char *program, const char *arg1, */
/* 			const char *arg2) */
/* { */
/* 	char *arg; */
/* 	size_t len; */
/* 	int res; */

/* 	len = os_strlen(arg1) + os_strlen(arg2) + 2; */
/* 	arg = os_malloc(len); */
/* 	if (arg == NULL) */
/* 		return -1; */
/* 	os_snprintf(arg, len, "%s %s", arg1, arg2); */
/* 	res = os_exec(program, arg, 1); */
/* 	os_free(arg); */

/* 	return res; */
/* } */

static int str_match(const char *a, const char *b)
{
	return os_strncmp(a, b, os_strlen(b)) == 0;
}

/* static int str_starts(const char *src, const char *match) */
/* { */
/* 	return os_strncmp(src, match, os_strlen(match)) == 0; */
/* } */

static int check_terminating(const char *msg)
{
	const char *pos = msg;

	if (*pos == '<') {
		/* skip priority */
		pos = os_strchr(pos, '>');
		if (pos)
			pos++;
		else
			pos = msg;
	}

	if (str_match(pos, WPA_EVENT_TERMINATING) && ctrl_conn) {
		/* edit_clear_line(); */
		printf("\rConnection to wpa_supplicant lost - trying to "
		       "reconnect\n");
		/* edit_redraw(); */
		wpa_cli_attached = 0;
		wpa_cli_close_connection();
		return 1;
	}

	return 0;
}

//------------------------------------------------------------------------------
//
// Action file
//
//------------------------------------------------------------------------------
static void action_ssh_keygen(const char *key_file_path)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "%s SSH-KEYGEN %s", ctrl_ifname, key_file_path);
	os_exec(action_file, cmd_buf, 1);
}

static void action_ssh_copy_id(const char *user, const char *ip_str, const char *public_key_file)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "%s SSH-COPY-ID %s %s %s", ctrl_ifname, user, ip_str,
		    public_key_file);
	os_exec(action_file, cmd_buf, 1);
}

static void action_scp_fetch(int pub_key_authorized,const char *user,
			     const char *priv_key_file, const char *ip_str,
			     const char *remote_file, const char *local_file,
			     int folder)
{
	if (pub_key_authorized == 0)
		action_ssh_copy_id(user, ip_str, pub_key_file);

	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "%s SCP-FETCH %s %s %s %s %s %s",
		    ctrl_ifname, user, priv_key_file, ip_str, remote_file, local_file,
		    folder ? "folder" : "file");
	os_exec(action_file, cmd_buf, 1);
}

/* static void action_ssh_restart() */
/* { */
/* 	os_snprintf(cmd_buf, sizeof(cmd_buf), */
/* 		    "%s SSH-RESTART", ctrl_ifname); */
/* 	os_exec(action_file, cmd_buf, 1); */
/* } */

/* static void action_wpas_restart(const char *ifname) */
/* { */
/* 	os_snprintf(cmd_buf, sizeof(cmd_buf), */
/* 		    "%s STOP-WPAS", ifname); */
/* 	os_exec(action_file, cmd_buf, 1); */

/* 	os_snprintf(cmd_buf, sizeof(cmd_buf), */
/* 		    "%s START-WPAS", ifname); */
/* 	os_exec(action_file, cmd_buf, 0); */
/* } */

/* static void action_if_restart(const char *ifname) */
/* { */
/* 	os_snprintf(cmd_buf, sizeof(cmd_buf), */
/* 		    "%s RESTART-IF", ifname); */
/* 	os_exec(action_file, cmd_buf, 1); */
/* } */

//------------------------------------------------------------------------------
//
// Local functions: command
//
//------------------------------------------------------------------------------
static void wpa_cli_msg_cb(char *msg, size_t len)
{
	printf("msg_cb: %s\n", msg);
}

static int _wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd, int print)
{
	char buf[4096];
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to wpa_supplicant - command dropped.\n");
		return -1;
	}
	if (ifname_prefix) {
		os_snprintf(buf, sizeof(buf), "IFNAME=%s %s",
			    ifname_prefix, cmd);
		buf[sizeof(buf) - 1] = '\0';
		cmd = buf;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, os_strlen(cmd), buf, &len,
			       wpa_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}
	if (print) {
		buf[len] = '\0';
		printf("%s", buf);
		if (interactive && len > 0 && buf[len - 1] != '\n')
			printf("\n");
	}
	if (str_starts(buf, "ASP_SEEK "))
		on_seek_service_return(buf + 9);
	else if (str_starts(buf, "ASP_CONNECT_SESSION "))
		on_connect_session_return(buf + 19);
	return 0;
}

static int wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd)
{
	printf("[ctrl]: sending '%s'\n", cmd);
	return _wpa_ctrl_command(ctrl, cmd, 1);
}

/* static int write_cmd(char *buf, size_t buflen, const char *cmd, int argc, */
/* 		     char *argv[]) */
/* { */
/* 	int i, res; */
/* 	char *pos, *end; */

/* 	pos = buf; */
/* 	end = buf + buflen; */

/* 	res = os_snprintf(pos, end - pos, "%s", cmd); */
/* 	if (os_snprintf_error(end - pos, res)) */
/* 		goto fail; */
/* 	pos += res; */

/* 	for (i = 0; i < argc; i++) { */
/* 		res = os_snprintf(pos, end - pos, " %s", argv[i]); */
/* 		if (os_snprintf_error(end - pos, res)) */
/* 			goto fail; */
/* 		pos += res; */
/* 	} */

/* 	buf[buflen - 1] = '\0'; */
/* 	return 0; */

/* fail: */
/* 	printf("Too long command\n"); */
/* 	return -1; */
/* } */

/* static int wpa_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd, int min_args, */
/* 		       int argc, char *argv[]) */
/* { */
/* 	char buf[4096]; */
/* 	if (argc < min_args) { */
/* 		printf("Invalid %s command - at least %d argument%s " */
/* 		       "required.\n", cmd, min_args, */
/* 		       min_args > 1 ? "s are" : " is"); */
/* 		return -1; */
/* 	} */
/* 	if (write_cmd(buf, sizeof(buf), cmd, argc, argv) < 0) */
/* 		return -1; */
/* 	return wpa_ctrl_command(ctrl, buf); */
/* } */

//------------------------------------------------------------------------------
//
// Local functions: wfds send
//
//------------------------------------------------------------------------------
static int ssh_read_self_pub_key(const char *pub_key_file, char *buf,
				 size_t buflen)
{
	FILE *fp;

	fp = fopen(pub_key_file, "r");
	if (fp == NULL)
	{
		action_ssh_keygen(priv_key_file);
		fp = fopen(pub_key_file, "r");
		if (fp == NULL) {
			printf("Failed to read %s: %s\n", pub_key_file,
			       strerror(errno));
			return 0;
		}
	}

	if (fgets(buf, buflen, fp) == NULL) {
		printf("fgets failed: %s\n", strerror(errno));
		return 0;
	}

	fclose(fp);
	return strlen(buf);
}

static int ssh_key_authorized(const char *pub_key_buf)
{
	FILE *fp;
	int authorized = 0;
	char buf[512];

	fp = fopen(authorized_key_file, "r");
	if (fp == NULL)
		return authorized;

	while(fgets(buf, sizeof(buf), fp)) {
		if (os_memcmp(pub_key_buf, buf, os_strlen(pub_key_buf)) == 0) {
			authorized = 1;
			break;
		}
	}

	fclose(fp);
	return authorized;
}

static int ssh_authorize_key(const char *pub_key_buf)
{
	FILE *fp;

	if (ssh_key_authorized(pub_key_buf))
	    return 0;

	fp = fopen(authorized_key_file, "a");
	if (fp == NULL)
	{
		printf("Failed to append %s: %s\n", authorized_key_file,
		       strerror(errno));
		return -1;
	}

	fputs("\n", fp);
	fputs(pub_key_buf, fp);

	fclose(fp);
	return 0;
}

static int cancel_seek(u32 search_id)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "ASP_CANCEL_SEEK search_id=%u", 0);
	return wpa_ctrl_command(ctrl_conn, cmd_buf);
}

static int seek_service(const char *service_name)
{
	/* int timeout = 10; */

	/* Cancel outstanding seek */
	if (cancel_seek(0) < 0)
		return -1;

	/* Seek service */
	/* os_snprintf(cmd_buf, sizeof(cmd_buf), */
	/* 	    "ASP_SEEK %d seek=%s", timeout, service_name); */
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "ASP_SEEK seek=%s", service_name);
	return wpa_ctrl_command(ctrl_conn, cmd_buf);
}

static int serv_disc(const u8 *peer, u32 adv_id, const char *service_name,
		     const char *service_info)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "P2P_SERV_DISC_REQ"
		    " "MACSTR
		    " asp"
		    " %u"
		    " %s"
		    " \'%s\'",
		    MAC2STR(peer), adv_id, service_name, service_info);
	return wpa_ctrl_command(ctrl_conn, cmd_buf);
}

static int advertise_service(const char *service_name,
			     const char *service_info)
{
	u8 auto_accept = 1;
	u8 service_state = 1;
	u16 config_methods = 0x1108;

	printf("Advertising %s with service info \'%s\'\n",
	       service_name, service_info);

	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "ASP_ADVERTISE %u %u %x %s svc_info=\'%s\'",
		    auto_accept, service_state, config_methods,
		    service_name, service_info);

	return wpa_ctrl_command(ctrl_conn, cmd_buf);
}

static void on_seek_service_return(const char *buf)
{
	const char * pos;
	long long unsigned val;

	pos = os_strstr(buf, "search_id=");
	if (!pos || sscanf(pos + 10, "%llx", &val) != 1 || val > 0xffffffffULL)
		return;
	search_id = val;
	printf("seek started with search_id: %u\n", search_id);
}

static int connect_session(u32 adv_id, const u8 *adv_mac,
			   const char *session_info)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "ASP_CONNECT_SESSION "MACSTR
		    " adv_id=%u"
		    " adv_mac="MACSTR
		    " method=1000",
		    MAC2STR(adv_mac), adv_id, MAC2STR(adv_mac));

	if (session_info)
		os_snprintf(cmd_buf + os_strlen(cmd_buf),
			    sizeof(cmd_buf) - os_strlen(cmd_buf),
			    " info=\'%s\'", session_info);

	return wpa_ctrl_command(ctrl_conn, cmd_buf);
}

static void on_connect_session_return(const char *buf)
{
	const char * pos;
	long long unsigned val;

	pos = os_strstr(buf, "session_id=");
	if (!pos || sscanf(pos + 11, "%llx", &val) != 1 || val > 0xffffffffULL)
		return;
	session_id = val;

	pos = os_strstr(buf, "session_mac=");
	if (!pos || hwaddr_aton(pos + 12, session_mac))
		return;

	printf("connect session started with ("MACSTR", %u)\n",
	       MAC2STR(session_mac), session_id);
}

static void remove_session(const u8 *session_mac, u32 session_id, u8 reason)
{
	os_snprintf(cmd_buf, sizeof(cmd_buf),
		    "ASP_REMOVE_SESSION "MACSTR" session_mac="MACSTR"\
session_id=%u reason=%u",
		    MAC2STR(session_mac), MAC2STR(session_mac), session_id,
		    reason);
	wpa_ctrl_command(ctrl_conn, cmd_buf);
}

/* static void rt_wfds_send_send_timeout(void *eloop_ctx, void *timeout_ctx) */
/* { */
/* 	os_snprintf(cmd_buf, sizeof(cmd_buf), */
/* 		    "ASP_REMOVE_SESSION "MACSTR" session_mac="MACSTR"\ */
/* session_id=%u reason=1", */
/* 		    MAC2STR(session_mac), MAC2STR(session_mac), session_id); */
/* 	wpa_ctrl_command(ctrl_conn, cmd_buf); */
/* } */

static void on_search_result(const char *event)
{
	const char * pos;
	long long unsigned val;

	/* Match device name */
	if (!os_strstr(event, dev_name))
		return;

	/* Parse for adv_id and adv_mac */
	pos = os_strstr(event, "adv_id=");
	if (!pos || sscanf(pos + 7, "%llx", &val) != 1 || val > 0xffffffffULL)
		return;
	adv_id = val;

	pos = os_strstr(event, "service_mac=");
	if (!pos || hwaddr_aton(pos + 12, adv_mac))
		return;

	/* Serv disc */
	serv_disc(adv_mac, adv_id, service_name, service_info_prefix);
}

static void on_session_status(const char *event)
{
	if (os_strstr(event, "Open")) {
		char *pos;
		const char *remote_ip;

		pos = os_strstr(event, "remote_ip=");
		if (pos == NULL)
			goto fail_event;
		remote_ip = pos + 10;

		if (os_strcmp(role, "seeker") == 0) {
			ssh_authorize_key(service_info);
			/* action_ssh_restart(); */
		} else {
			const char *user, *remote_file, *file_type,
				*authorized;

			/* Parse session info */
			/* Format:  user:file:ftype:authorized*/
			pos = os_strchr(session_info, ':');
			if (pos == NULL)
				goto fail_session_info;
			user = session_info;
			*pos++ = '\0';
			remote_file = pos;
			pos = os_strchr(pos, ':');
			*pos++ = '\0';
			file_type = pos;
			pos = os_strchr(pos, ':');
			*pos++ = '\0';
			authorized = pos;

			/* Fetch file */
			action_scp_fetch(os_strcmp(authorized, "authorized") == 0,
					 user, priv_key_file, remote_ip, remote_file,
					 "./",
					 os_strcmp(file_type, "folder") == 0);

			/* Close session */
			remove_session(session_mac, session_id, 1);
		}
	}

	return;

fail_event:
	printf("Invalid session status event: %s\n", event);
	return;

fail_session_info:
	printf("Invalid session info: %s\n", session_info);
	return;
}

static void on_asp_status(const char *event)
{
	if (os_strstr(event, "AllSessionClosed")) {
		os_snprintf(cmd_buf, sizeof(cmd_buf), "P2P_GROUP_REMOVE %s",
			    ctrl_ifname);
		wpa_ctrl_command(ctrl_conn, cmd_buf);
	}
}

static void on_connect_status(const char *event)
{
	if (os_strstr(event, "Disconnected")) {
		if (os_strcmp(role, "seeker") == 0)
			eloop_terminate();
		else {
			/* action_wpas_restart(ctrl_ifname); */
		}
	} else if (os_strstr(event, "GroupFormationFailed")) {
		printf("Fail: Group Formation failed: %s\n", event);
		eloop_terminate();
	}
}

static void on_serv_asp_resp(const char *event)
{
	char *pos, *pos2;
	size_t info_len;

	/* Parse service_info */
	pos = os_strchr(event, '\'');
	if (pos == NULL)
		goto fail;
	pos++;
	pos2 = os_strchr(pos + 1, '\'');
	if (pos2 == NULL)
		goto fail;
	info_len = (size_t) (pos2 - pos);

	service_info[info_len] = '\0';
	os_memcpy(service_info, pos, info_len);

	printf("service_info: \'%s\'\n", service_info);

	/* Construct session info */

	/* This does not work although it creates correct string in PD req */
	/* os_snprintf(session_info, sizeof(session_info), */
	/* 	    "user=\\\'%s\\\' file_name=\\\'%s\\\' file_type=\\\'%s\\\'", */
	/* 	    "root", file_name, file_type); */

	os_snprintf(session_info, sizeof(session_info),
		    "%s:%s:%s:%s", user, file_name, file_type,
		    ssh_key_authorized(service_info) ?
		    "authorized" : "unauthorized");

	/* Connect session */
	connect_session(adv_id, adv_mac, session_info);

	return;
fail:
	printf("Invalid service info, event: %s\n", event);
	return;
}

static void on_session_req(const char *event)
{
	char *pos, *pos2;
	long long unsigned val;
	size_t info_len;

	pos = os_strstr(event, "session_id=");
	if (!pos || sscanf(pos + 11, "%llx", &val) != 1 || val > 0xffffffffULL)
		return;
	session_id = val;

	pos = os_strstr(event, "session_mac=");
	if (!pos || hwaddr_aton(pos + 12, session_mac))
		goto fail;

	pos = os_strstr(event, "session_info=\'");
	if (pos == NULL)
		goto fail;
	pos += 14;
	pos2 = os_strchr(pos, '\'');
	if (pos2 == NULL)
		goto fail;

	info_len = (size_t) (pos2 - pos);

	session_info[info_len] = '\0';
	os_memcpy(session_info, pos, info_len);

	printf("session_info: \'%s\'\n", session_info);

	return;

fail:
	printf("Invalid session info, event: %s\n", event);
	return;
}

static void process_event(const char *event)
{
	const char *start;

	start = os_strchr(event, '>');
	if (start == NULL)
		return;

	start++;

	if (str_starts(start, "ASP-SEARCH-RESULT"))
		on_search_result(start + 18);
	else if (str_starts(start, "P2P-SERV-ASP-RESP"))
		on_serv_asp_resp(start + 18);
	else if (str_starts(start, "ASP-SESSION-STATUS"))
		on_session_status(start + 19);
	else if (str_starts(start, "ASP-STATUS"))
		on_asp_status(start + 11);
	else if (str_starts(start, "ASP-CONNECT-STATUS"))
		on_connect_status(start + 18);
	else if (str_starts(start, "ASP-SESSION-REQ"))
		on_session_req(start + 15);
}

static int startup()
{
	if (os_strcmp(role, "seeker") == 0)
		return seek_service(service_name);
	else {

		if(ssh_read_self_pub_key(pub_key_file, service_info_str,
					 sizeof(service_info_str)) <= 0) {
			printf("Failed to read self public key\n");
			return -1;
		}

		return advertise_service(service_name, service_info_str);

	}
}

//------------------------------------------------------------------------------
//
// Local functions: event
//
//------------------------------------------------------------------------------
static int wpa_cli_show_event(const char *event)
{
	const char *start;

	start = os_strchr(event, '>');
	if (start == NULL)
		return 1;

	start++;
	/*
	 * Skip BSS added/removed events since they can be relatively frequent
	 * and are likely of not much use for an interactive user.
	 */
	/* if (str_starts(start, WPA_EVENT_BSS_ADDED) || */
	/*     str_starts(start, WPA_EVENT_BSS_REMOVED)) */
	/* 	return 0; */

	if (str_starts(start, "ASP"))
		return 1;

	if (str_starts(start, "P2P-SERV-ASP-RESP") &&
	    os_strcmp(role, "seeker") == 0 &&
		serv_asp_resp_processed == 0) {
		serv_asp_resp_processed = 1;
		return 1;
	}

	return 0;
}

static void wpa_cli_recv_pending(struct wpa_ctrl *ctrl, int action_monitor)
{
	if (ctrl_conn == NULL) {
		wpa_cli_reconnect();
		return;
	}
	while (wpa_ctrl_pending(ctrl) > 0) {
		char buf[4096];
		size_t len = sizeof(buf) - 1;
		if (wpa_ctrl_recv(ctrl, buf, &len) == 0) {
			buf[len] = '\0';
			if (action_monitor) {
				/* wpa_cli_action_process(buf); */
			} else {
				/* cli_event(buf); */
				if (wpa_cli_show_event(buf)) {
					//edit_clear_line();
					printf("\r%s\n", buf);
					process_event(buf);
					/* edit_redraw(); */
				}

				if (interactive && check_terminating(buf) > 0)
					return;
			}
		} else {
			printf("Could not read pending message.\n");
			break;
		}
	}

	if (wpa_ctrl_pending(ctrl) < 0) {
		printf("Connection to wpa_supplicant lost - trying to "
		       "reconnect\n");
		wpa_cli_reconnect();
	}
}

static void wpa_cli_mon_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	wpa_cli_recv_pending(mon_conn, 0);
}


static void wpa_cli_terminate(int sig, void *ctx)
{
	eloop_terminate();
}

//------------------------------------------------------------------------------
//
// Local functions: wpa_s connection
//
//------------------------------------------------------------------------------

static int wpa_cli_open_connection(const char *ifname, int attach)
{
#if defined(CONFIG_CTRL_IFACE_UDP) || defined(CONFIG_CTRL_IFACE_NAMED_PIPE)
	ctrl_conn = wpa_ctrl_open(ifname);
	if (ctrl_conn == NULL)
		return -1;

	if (attach && interactive)
		mon_conn = wpa_ctrl_open(ifname);
	else
		mon_conn = NULL;
#else /* CONFIG_CTRL_IFACE_UDP || CONFIG_CTRL_IFACE_NAMED_PIPE */
	char *cfile = NULL;
	int flen, res;

	if (ifname == NULL)
		return -1;

#ifdef ANDROID
	if (access(ctrl_iface_dir, F_OK) < 0) {
		cfile = os_strdup(ifname);
		if (cfile == NULL)
			return -1;
	}
#endif /* ANDROID */

	if (client_socket_dir && client_socket_dir[0] &&
	    access(client_socket_dir, F_OK) < 0) {
		perror(client_socket_dir);
		os_free(cfile);
		return -1;
	}

	if (cfile == NULL) {
		flen = os_strlen(ctrl_iface_dir) + os_strlen(ifname) + 2;
		cfile = os_malloc(flen);
		if (cfile == NULL)
			return -1;
		res = os_snprintf(cfile, flen, "%s/%s", ctrl_iface_dir,
				  ifname);
		if (os_snprintf_error(flen, res)) {
			os_free(cfile);
			return -1;
		}
	}

	ctrl_conn = wpa_ctrl_open2(cfile, client_socket_dir);
	if (ctrl_conn == NULL) {
		os_free(cfile);
		return -1;
	}

	if (attach && interactive)
		mon_conn = wpa_ctrl_open2(cfile, client_socket_dir);
	else
		mon_conn = NULL;
	os_free(cfile);
#endif /* CONFIG_CTRL_IFACE_UDP || CONFIG_CTRL_IFACE_NAMED_PIPE */

	if (mon_conn) {
		if (wpa_ctrl_attach(mon_conn) == 0) {
			wpa_cli_attached = 1;
			if (interactive)
				eloop_register_read_sock(
					wpa_ctrl_get_fd(mon_conn),
					wpa_cli_mon_receive, NULL, NULL);
		} else {
			printf("Warning: Failed to attach to "
			       "wpa_supplicant.\n");
			wpa_cli_close_connection();
			return -1;
		}
	}

	return 0;
}

static void wpa_cli_close_connection(void)
{
	if (ctrl_conn == NULL)
		return;

	if (wpa_cli_attached) {
		wpa_ctrl_detach(interactive ? mon_conn : ctrl_conn);
		wpa_cli_attached = 0;
	}
	wpa_ctrl_close(ctrl_conn);
	ctrl_conn = NULL;
	if (mon_conn) {
		eloop_unregister_read_sock(wpa_ctrl_get_fd(mon_conn));
		wpa_ctrl_close(mon_conn);
		mon_conn = NULL;
	}
}

static void wpa_cli_cleanup(void)
{
	wpa_cli_close_connection();
	/* if (pid_file) */
	/* 	os_daemonize_terminate(pid_file); */

	os_program_deinit();
}

static void try_connection(void *eloop_ctx, void *timeout_ctx)
{
	if (ctrl_conn)
		goto done;

	if (ctrl_ifname == NULL)
		return;

	if (wpa_cli_open_connection(ctrl_ifname, 1)) {
		/* if (!warning_displayed) { */
		/* 	printf("Could not connect to wpa_supplicant: " */
		/* 	       "%s - re-trying\n", */
		/* 	       ctrl_ifname ? ctrl_ifname : "(nil)"); */
		/* 	warning_displayed = 1; */
		/* } */
		eloop_register_timeout(1, 0, try_connection, NULL, NULL);
		return;
	}

/* 	update_bssid_list(ctrl_conn); */
/* 	update_networks(ctrl_conn); */

/* 	if (warning_displayed) */
	printf("Connection established.\n");

done:
	eloop_register_timeout(ping_interval, 0, wpa_cli_ping, NULL, NULL);

	startup();

	return;

/* 	start_edit(); */
}

static void wpa_cli_reconnect(void)
{
	wpa_cli_close_connection();
	if (wpa_cli_open_connection(ctrl_ifname, 1) < 0) {
		printf("Failed to open connection to wpa_s\n");
		return;
	}

	/* if (interactive) { */
	/* 	edit_clear_line(); */
		printf("\rConnection to wpa_supplicant re-established\n");
	/* 	edit_redraw(); */
	/* } */

	startup();

}

static void wpa_cli_ping(void *eloop_ctx, void *timeout_ctx)
{
	if (ctrl_conn) {
		int res;
		char *prefix = ifname_prefix;

		ifname_prefix = NULL;
		res = _wpa_ctrl_command(ctrl_conn, "PING", 0);
		ifname_prefix = prefix;
		if (res) {
			printf("Connection to wpa_supplicant lost - trying to "
			       "reconnect\n");
			wpa_cli_close_connection();
		}
	}
	if (!ctrl_conn)
		wpa_cli_reconnect();
	eloop_register_timeout(ping_interval, 0, wpa_cli_ping, NULL, NULL);
}

static void wpa_cli_interactive(void)
{
	eloop_register_timeout(0, 0, try_connection, NULL, NULL);
	eloop_run();
	eloop_cancel_timeout(try_connection, NULL, NULL);

	/* cli_txt_list_flush(&p2p_peers); */
	/* cli_txt_list_flush(&p2p_groups); */
	/* cli_txt_list_flush(&bsses); */
	/* cli_txt_list_flush(&ifnames); */
	/* cli_txt_list_flush(&networks); */
	/* if (edit_started) */
	/* 	edit_deinit(hfile, wpa_cli_edit_filter_history_cb); */
	/* os_free(hfile); */
	eloop_cancel_timeout(wpa_cli_ping, NULL, NULL);
	wpa_cli_close_connection();
}

//------------------------------------------------------------------------------
//
// main
//
//------------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	int c;

	if (os_program_init())
		return -1;

	for (;;) {
		c = getopt(argc, argv, "d:i:f:r:t:u:");
		if (c < 0)
			break;
		switch (c) {
		case 'd':
			os_free(dev_name);
			dev_name = os_strdup(optarg);
			break;
		case 'i':
			os_free(ctrl_ifname);
			ctrl_ifname = os_strdup(optarg);
			break;
		case 'f':
			file_name = os_strdup(optarg);
			break;
		case 'r':
			role = os_strdup(optarg);
			break;
		case 't':
			file_type = os_strdup(optarg);
			break;
		case 'u':
			user = os_strdup(optarg);
			break;
		default:
			return -1;
		}
	}

	if (ctrl_ifname == NULL)
		return -1;

	if (role == NULL)
		return -1;

	if (os_strcmp(role, "seeker") == 0 && dev_name == NULL)
		return -1;

	if (os_strcmp(role, "seeker") == 0 && file_name == NULL)
		return -1;

	if (os_strcmp(role, "seeker") == 0 && user == NULL)
		return -1;

	if (file_name && file_type == NULL)
		return -1;

	if (eloop_init())
		return -1;

	/* action_wpas_restart(ctrl_ifname); */

	/* Generate authorized key file path */
	if (os_strcmp(role, "seeker") == 0) {
		os_snprintf(authorized_key_file, sizeof(authorized_key_file),
			    "/home/%s/.ssh/authorized_keys", user);
		printf("authorized key file: %s\n", authorized_key_file);
	}

	eloop_register_signal_terminate(wpa_cli_terminate, NULL);

	interactive = 1;
	wpa_cli_interactive();

	os_free(dev_name);
	os_free(ctrl_ifname);
	os_free(file_name);
	os_free(file_type);
	os_free(user);
	eloop_destroy();
	wpa_cli_cleanup();

	return 0;
}
#else /* CONFIG_CTRL_IFACE */
int main(int argc, char *argv[])
{
	printf("CONFIG_CTRL_IFACE not defined - rt_wfds_send disabled\n");
	return -1;
}
#endif /* CONFIG_CTRL_IFACE */
