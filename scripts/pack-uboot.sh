#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0

function do_prepare()
{
	export PATH=$EXTER/packages/pack-uboot/${BOARDFAMILY}/tools/:$PATH
	cp ${EXTER}/packages/pack-uboot/${BOARDFAMILY}/bin/* . -r
	cp sys_config/sys_config_${BOARD}.fex sys_config.fex
}

function do_ini_to_dts()
{
	local DTC_COMPILER=$EXTER/packages/pack-uboot/${BOARDFAMILY}/tools/dtc
	[[ ! -f $DTC_COMPILER ]] && exit_with_error "Script_to_dts: Can not find dtc compiler."

	if [[ $BOARDFAMILY =~ sun50iw2|sun50iw6|sun50iw9 ]]; then

		#Disbale noisy checks
		local DTC_FLAGS="-W no-unit_address_vs_reg"

		$DTC_COMPILER -p 2048 ${DTC_FLAGS} -@ -O dtb -o ${BOARD}-u-boot.dtb -b 0 dts/${BOARD}-u-boot.dts >/dev/null 2>&1

		[[ $? -ne 0 ]] && exit_with_error "dtb: Conver script to dts failed."

		# It'is used for debug dtb
		$DTC_COMPILER ${DTC_FLAGS} -I dtb -O dts -o .${BOARD}-u-boot.dts ${BOARD}-u-boot.dtb >/dev/null 2>&1
	fi
}

function do_common()
{
	unix2dos sys_config.fex > /dev/null 2>&1
	script  sys_config.fex  > /dev/null 2>&1
	cp ${PACKOUT_DIR}/${BOARD}-u-boot.dtb sunxi.fex

	if [[ $BOARDFAMILY =~ sun50iw6|sun50iw9 ]]; then
		update_dtb sunxi.fex 4096 >/dev/null
	fi
	
	update_boot0 boot0_sdcard.fex	sys_config.bin SDMMC_CARD > /dev/null
	if [[ $BOARDFAMILY =~ sun50iw6|sun50iw9 ]]; then
		update_uboot -no_merge u-boot.fex sys_config.bin > /dev/null
	fi
	[[ $? -ne 0 ]] && exit_with_error "update u-boot run error"

	#pack boot package
	unix2dos boot_package.cfg > /dev/null 2>&1
	dragonsecboot -pack boot_package.cfg > /dev/null
	[[ $? -ne 0 ]] && exit_with_error "dragon pack error"

}

pack_uboot()
{
	PACKOUT_DIR=$SRC/.tmp/packout
	cd ${PACKOUT_DIR}

	do_prepare
	do_ini_to_dts
	do_common
}
