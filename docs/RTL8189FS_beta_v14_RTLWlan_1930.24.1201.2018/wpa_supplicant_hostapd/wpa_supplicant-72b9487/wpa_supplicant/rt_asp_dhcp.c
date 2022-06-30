//---------------------------------------------------------------------------
//
// Copyright (c) 2016 Realtek Semiconductor, Inc. All rights reserved.
//
//---------------------------------------------------------------------------
//

#include "rt_asp_i.h"
#include <stdio.h>

#define ASP_DHCP_SH "examples/p2p-action.sh"
#define ASP_DHCPD_LOG "/var/lib/misc/dnsmasq.leases"
#define ASP_DHCPC_LF_PAT "/var/run/dhclient.leases-" /* intf name after the dash */

struct in_addr asp_dhcp_parse_dhcpd_lease(const struct asp_context *ctx,
					  const u8 *mac)
{
	FILE *file;
	char line[100];
	char mac_txt[3 * ETH_ALEN];
	char *pch = NULL;
	struct in_addr addr = {0};

	file = fopen(ASP_DHCPD_LOG, "r");
	if (file == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG": failed to open %s for read\n",
			   ASP_DHCPD_LOG);
		return addr;
	}

	/* Parse the lease file line by line.
	 * The line would be look like:
	 *     1457702130 7c:c7:09:8d:95:09 192.168.42.51
	 *         APPLE3FD1 01:7c:c7:09:8d:95:09 */
	while (fgets(line, sizeof(line), file)) {
		/* Match mac addr */
		os_snprintf(mac_txt, sizeof(mac_txt), MACSTR, MAC2STR(mac));
		if (os_strstr(line, mac_txt) == NULL)
			continue;

		/* Mac addr match, parse ip addr */
		pch = strtok(line, " ");
		while (pch && os_strstr(pch, "192.168") == NULL)
			pch = strtok(NULL, " ");

		if (pch)
			break;
	}

	if (pch) {
		/* Translate from ascii to value */
		if (inet_aton(pch, &addr) == 0) {
			wpa_printf(MSG_ERROR,
				   ASP_TAG": invalid addr: %s\n", pch);
		}
	}

	if (file)
		fclose(file);

	return addr;
}

struct in_addr asp_dhcp_parse_dhcpc_lease(const char * ifname)
{
	char *lease_file;
	char *p;
	FILE *file;
	char line[100];
	char *pch = NULL;
	int intf_found = 0;
	struct in_addr addr = {0};
	char *ip_txt = NULL;

	/* Generate lease file name from the pattern */
	lease_file = os_zalloc(strlen(ASP_DHCPC_LF_PAT) + os_strlen(ifname) + 1);
	if (lease_file == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": Failed to allocate memory for lease_file");
		return addr;
	}

	os_memcpy(lease_file, ASP_DHCPC_LF_PAT, os_strlen(ASP_DHCPC_LF_PAT) + 1);
	p = lease_file + os_strlen(lease_file);
	os_memcpy(p, ifname, os_strlen(ifname) + 1);

	/* Parse lease file */
	file = fopen(lease_file, "r");
	if (file == NULL) {
		wpa_printf(MSG_ERROR, ASP_TAG": failed to open %s for read\n",
			   lease_file);
		os_free(lease_file);
		return addr;
	}

	while (fgets(line, sizeof(line), file)) {
		if (intf_found == 0) {
			/* Match interface "wlan1" */
			if (os_strstr(line, "interface") &&
			    os_strstr(line, ifname))
				intf_found = 1;
		} else if (os_strchr(line, '}')) {
			/* Running out of current lease range */
			break;
		} else if ((pch = os_strstr(line, "dhcp-server-identifier "))) {
			/*  option dhcp-server-identifier 192.168.196.1; */
			char *c = pch + os_strlen("dhcp-server-identifier ");
			ip_txt = strtok(c, ";");
			break;
		}
	}

	if (ip_txt && inet_aton(ip_txt, &addr))
		wpa_printf(MSG_INFO, ASP_TAG": Got ip addr: %s\n", ip_txt);

	/* Cleanup */
	os_free(lease_file);
	if (file)
		fclose(file);

	return addr;
}

void asp_dhcp_start(struct asp_context *ctx,
		    int go, const char *ssid_txt, int freq,
		    const char *psk_txt, const char *passphrase,
		    const u8 *go_dev_addr, int persistent,
		    const char *extra)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	char *buf;
	size_t buflen = 256;

	/* Generate msg for starting dhcp */
	buf = (char *)os_zalloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   "asp_on_p2p_group_started: Failed to allocate"
			   "msg buffer");
		return;
	}

	os_snprintf(buf, buflen,
		    "%s " P2P_EVENT_GROUP_STARTED
		    "%s %s ssid=\"%s\" freq=%d%s%s%s%s%s go_dev_addr="
		    MACSTR "%s%s", wpa_s->ifname, wpa_s->ifname,
		    go ? "GO" : "client", ssid_txt, freq, " psk=", psk_txt,
		    passphrase ? " passphrase=\"" : "",
		    passphrase ? passphrase : "", passphrase ? "\"" : "",
		    MAC2STR(go_dev_addr),
		    persistent ? " [PERSISTENT]" : "", extra);

	wpa_printf(MSG_INFO, ASP_TAG": asp_dhcp_start: prgram %s, arg: %s\n",
		   ASP_DHCP_SH, buf);

	/* Start dhcpc/dhcpd */
	os_exec(ASP_DHCP_SH, buf, 0);

	bin_clear_free(buf, buflen);
}

void asp_dhcp_stop(struct asp_context *ctx, int go,
		   const u8 *ssid, size_t ssid_len,
		   const char *reason)
{
	struct wpa_supplicant *wpa_s = ctx->wpa_s;
	char *buf;
	size_t buflen = 256;

	/* Generate msg for stopping dhcp */
	buf = (char *)os_zalloc(256);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   ASP_TAG": asp_dhcp_stop: Failed to allocate"
			   "msg buffer");
		return;
	}

	os_snprintf(buf, buflen,
		    "%s " P2P_EVENT_GROUP_REMOVED "%s %s%s",
		    wpa_s->ifname, wpa_s->ifname, go ? "GO" : "client", reason);

	os_exec(ASP_DHCP_SH, buf, 0);

	bin_clear_free(buf, buflen);
}
