===============================================================================
		Software Package - Component
===============================================================================
	1. readme.txt

	2. document/
		2.1 How_to_append_vendor_specific_ie_to_driver_management_frames.pdf
		2.2 How_to_set_driver_debug_log_level.pdf
		2.3 HowTo_enable_and_verify_TDLS_function_in_Wi-Fi_driver.pdf
		2.4 HowTo_enable_driver_to_support_80211d.pdf
		2.5 HowTo_enable_driver_to_support_WIFI_certification_test.pdf
		2.6 HowTo_enable_the_power_saving_functionality.pdf
		2.7 HowTo_port_wireless_driver_to_Google_ChromiumOS.pdf
		2.8 linux_dhcp_server_notes.txt
		2.9 Miracast_for_Realtek_WiFi.pdf
		2.10 Quick_Start_Guide_for_Adaptivity_and_Carrier_Sensing_Test.pdf
		2.11 Quick_Start_Guide_for_Bridge.pdf
		2.12 Quick_Start_Guide_for_Driver_Compilation_and_Installation.pdf
		2.13 Quick_Start_Guide_for_SoftAP.pdf
		2.14 Quick_Start_Guide_for_Station_Mode.pdf
		2.15 Quick_Start_Guide_for_WOW.pdf
		2.16 Realtek_WiFi_concurrent_mode_Introduction.pdf
		2.17 RTK_P2P_WFD_Programming_guide.pdf
		2.18 SoftAP_Mode_features.pdf
		2.19 Wireless_tools_porting_guide.pdf
		2.20 wpa_cli_with_wpa_supplicant.pdf
		2.21 Driver_Configuration_for_RF_Regulatory_Certification_V1.pdf

	3. driver/
		3.1 BETA_Vx_RTLWlan_xxxx.xx.xxxx.xxxx.zip			

	4. wpa_supplicant_hostapd/
		4.1 wpa_supplicant_hostapd-0.8_rtw_r7475.20130812.tar.gz
			4.1.1 wpa_supplicant
				The tool help the wlan network to communicate under the
				protection of WPAPSK mechanism (WPA/WPA2) and add WPS patch
			4.1.2 hostapd

		4.2 wpa_0_8.conf
			 Configure file sample for wpa_supplicant-0.8

		4.3 rtl_hostapd_2G.conf
			 Configure files for Soft-AP mode 2.4G

		4.4 p2p_hostapd.conf
			 Configure file for hostapd for Wi-Fi Direct (P2P)

		4.5 wpa_supplicant_2_6_rxxxxx.tar.gz
			

	5. wireless_tools/
		5.1 wireless_tools.30.rtl.tar.gz

	 
	6. btcoex/
		12.1 HowTo_debug_BT_coexistence.pdf
		12.2 How_to_set_bt-coex_antenna_isolation_parameters_on_combo_card.pdf

	7. install.sh
		Script to compile and install WiFi driver easily in PC-Linux

==================================================================================================================
		User Guide for Driver compilation and installation
==================================================================================================================
			(*) Please refer to document/Quick_Start_Guide_for_Driver_Compilation_and_Installation.pdf
==================================================================================================================
		User Guide for Station mode
==================================================================================================================
			(*) Please refer to document/Quick_Start_Guide_for_Station_Mode.pdf
==================================================================================================================
		User Guide for Soft-AP mode
==================================================================================================================
			(*) Please refer to document/Quick_Start_Guide_for_SoftAP.pdf
			(*) Please use wpa_supplicant_hostapd-0.8_rtw_r7475.20130812.tar.gz
			(*) Please refer to document/linux_dhcp_server_notes.txt
==================================================================================================================
		User Guide for Wi-Fi Direct
==================================================================================================================
		Realtek Legacy Wi-Fi Direct:
			(*) Please refer to document/RTK_P2P_WFD_Programming_guide.pdf
			(*) Please use wpa_supplicant_hostapd-0.8_rtw_r7475.20130812.tar.gz
			(*) Please refer to document/linux_dhcp_server_notes.txt
			(*) Please refer to WiFi_Direct_User_Interface/
		Wi-Fi Direct with nl80211
			(*) Please use:
					wpa_supplicant_2_6_rxxxxx.tar.gz
			(*) For P2P instruction/command, please refer to:
					README-P2P inside the wpa_supplicant folder of the wpa_supplicant_8 you choose
			(*) For DHCP server, please refer to:
					document/linux_dhcp_server_notes.txt
==================================================================================================================
		User Guide for WPS2.0
==================================================================================================================
			(*) Please use:
					wpa_supplicant_hostapd-0.8_rtw_r7475.20130812.tar.gz
				or
					wpa_supplicant_2_6_rxxxxx.tar.gz
			(*) For WPS instruction/command, please refert to:
					README-WPS inside the wpa_supplicant folder of the wpa_supplicant_8 you choose
==================================================================================================================
		User Guide for Power Saving Mode
==================================================================================================================
			(*) Please refer to document/HowTo_enable_the_power_saving_functionality.pdf

