/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 */

enum axp305_reg {
	AXP305_CHIP_VERSION = 0x3,
	AXP305_OUTPUT_CTRL1 = 0x10,
	AXP305_DCDCD_VOLTAGE = 0x15,
	AXP305_SHUTDOWN = 0x32,
};

#define AXP305_CHIP_VERSION_MASK	0xcf

#define AXP305_OUTPUT_CTRL1_DCDCD_EN	(1 << 3)

#define AXP305_POWEROFF			(1 << 7)

//-----------------------------
// axp1530

#define	AXP1530_VERSION		(0x03)

//PMIC chip id reg03:bit7-6  bit3-
#define   AXP1530_CHIP_ID              (0x48)
#define   AXP313A_CHIP_ID              (0x4B)
#define   AXP313B_CHIP_ID              (0x4C)

/* define AXP1530 REGISTER */
#define	AXP1530_POWER_ON_SOURCE_INDIVATION			(0x00)
#define	AXP1530_POWER_OFF_SOURCE_INDIVATION			(0x01)
#define	AXP1530_VERSION								(0x03)
#define	AXP1530_OUTPUT_POWER_ON_OFF_CTL				(0x10)
#define AXP1530_DCDC_DVM_PWM_CTL					(0x12)
#define	AXP1530_DC1OUT_VOL							(0x13)
#define	AXP1530_DC2OUT_VOL          				(0x14)
#define	AXP1530_DC3OUT_VOL          				(0x15)
#define	AXP1530_ALDO1OUT_VOL						(0x16)
#define	AXP1530_DLDO1OUT_VOL						(0x17)
#define	AXP1530_POWER_DOMN_SEQUENCE					(0x1A)
#define	AXP1530_PWROK_VOFF_SERT						(0x1B)
#define AXP1530_POWER_WAKEUP_CTL					(0x1C)
#define AXP1530_OUTPUT_MONITOR_CONTROL				(0x1D)
#define	AXP1530_POK_SET								(0x1E)
#define	AXP1530_IRQ_ENABLE							(0x20)
#define	AXP1530_IRQ_STATUS							(0x21)
#define AXP1530_WRITE_LOCK							(0x70)
#define AXP1530_ERROR_MANAGEMENT					(0x71)
#define	AXP1530_DCDC1_2_POWER_ON_DEFAULT_SET		(0x80)
#define	AXP1530_DCDC3_ALDO1_POWER_ON_DEFAULT_SET	(0x81)

