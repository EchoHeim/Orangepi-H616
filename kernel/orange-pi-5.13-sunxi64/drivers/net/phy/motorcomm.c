/*
 * drivers/net/phy/motorcomm.c
 *
 * Driver for Motorcomm PHYs
 *
 * Author: yinghong.zhang<yinghong.zhang@motor-comm.com>
 *
 * Copyright (c) 2019 Motorcomm, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : Motorcomm Phys:
 *		Giga phys: yt8511, yt8521, yt8531, yt8614, yt8618
 *		100/10 Phys : yt8512, yt8512b, yt8510
 *		Automotive 100Mb Phys : yt8010
 *		Automotive 100/10 hyper range Phys: yt8510
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

/* for wol feature, 20210604 */
#include <linux/netdevice.h>

/********************************************
 **** configuration section begin ***********/

/* if system depends on ethernet packet to restore from sleep,
 * please define this macro to 1 otherwise, define it to 0.
 */
#define SYS_WAKEUP_BASED_ON_ETH_PKT     1

/* to enable system WOL feature of phy, please define this macro to 1
 * otherwise, define it to 0.
 */
#define YTPHY_WOL_FEATURE_ENABLE        0

/* some GMAC need clock input from PHY, for eg., 125M, please enable this macro
 * by degault, it is set to 0
 * NOTE: this macro will need macro SYS_WAKEUP_BASED_ON_ETH_PKT to set to 1
 */
#define GMAC_CLOCK_INPUT_NEEDED         1

#define YT8512_CLOCK_INIT_NEED			0

/* the max number of yt8521 chip on pcb board
 * the most case is only 1 chip per board, but
 * by default, we support up to 8.
 */
#define YTPHY_BOARD_MAX_NUM_OF_CHIP_8521	8
#define YTPHY_BOARD_MAX_NUM_OF_CHIP_8614	4

/* for YT8531 package A xtal init config */
#define YTPHY8531A_XTAL_INIT                (0)

/**** configuration section end ***********
 ******************************************/

/* no need to change below */

/* MOTORCOMM_PHY_H */
#define MOTORCOMM_PHY_ID_MASK           0x00000fff
#define MOTORCOMM_PHY_ID_8531_MASK		0xffffffff
#define MOTORCOMM_MPHY_ID_MASK          0x0000ffff
#define MOTORCOMM_MPHY_ID_MASK_8614		0xffffffff
#define MOTORCOMM_PHY_ID_MASK_8821		0xffffffff

#define PHY_ID_YT8010                   0x00000309
#define PHY_ID_YT8510                   0x00000109
#define PHY_ID_YT8511                   0x0000010a
#define PHY_ID_YT8512                   0x00000118
#define PHY_ID_YT8512B                  0x00000128
#define PHY_ID_YT8521                   0x0000011a
#define PHY_ID_YT8531S					0x4f51e91a
#define PHY_ID_YT8531					0x4f51e91b
#define PHY_ID_YT8614					0x4F51E899
#define PHY_ID_YT8618					0x0000e889
#define PHY_ID_YT8821					0x4f51ea10

#define REG_PHY_SPEC_STATUS             0x11
#define REG_DEBUG_ADDR_OFFSET           0x1e
#define REG_DEBUG_DATA                  0x1f

#define YT8512_EXTREG_AFE_PLL           0x50
#define YT8512_EXTREG_EXTEND_COMBO      0x4000
#define YT8512_EXTREG_LED0              0x40c0
#define YT8512_EXTREG_LED1              0x40c3

#define YT8512_EXTREG_SLEEP_CONTROL1    0x2027

#define YT_SOFTWARE_RESET		0x8000

#define YT8512_CONFIG_PLL_REFCLK_SEL_EN	0x0040
#define YT8512_CONTROL1_RMII_EN		0x0001
#define YT8512_LED0_ACT_BLK_IND		0x1000
#define YT8512_LED0_DIS_LED_AN_TRY	0x0001
#define YT8512_LED0_BT_BLK_EN		0x0002
#define YT8512_LED0_HT_BLK_EN		0x0004
#define YT8512_LED0_COL_BLK_EN		0x0008
#define YT8512_LED0_BT_ON_EN		0x0010
#define YT8512_LED1_BT_ON_EN		0x0010
#define YT8512_LED1_TXACT_BLK_EN	0x0100
#define YT8512_LED1_RXACT_BLK_EN	0x0200
#define YT8512_SPEED_MODE		0xc000
#define YT8512_DUPLEX			0x2000

#define YT8512_SPEED_MODE_BIT		14
#define YT8512_DUPLEX_BIT		13
#define YT8512_EN_SLEEP_SW_BIT		15

#define YT8521_EXTREG_SLEEP_CONTROL1	0x27
#define YT8521_EN_SLEEP_SW_BIT		15

#define YT8521_SPEED_MODE		0xc000
#define YT8521_DUPLEX			0x2000
#define YT8521_SPEED_MODE_BIT		14
#define YT8521_DUPLEX_BIT		13
#define YT8521_LINK_STATUS_BIT		10

/* based on yt8521 wol feature config register */
#define YTPHY_UTP_INTR_REG              0x12
/* WOL Feature Event Interrupt Enable */
#define YTPHY_WOL_FEATURE_INTR                  BIT(6)

/* Magic Packet MAC address registers */
#define YTPHY_WOL_FEATURE_MACADDR2_4_MAGIC_PACKET    0xa007
#define YTPHY_WOL_FEATURE_MACADDR1_4_MAGIC_PACKET    0xa008
#define YTPHY_WOL_FEATURE_MACADDR0_4_MAGIC_PACKET    0xa009

#define YTPHY_WOL_FEATURE_REG_CFG               0xa00a
#define YTPHY_WOL_FEATURE_TYPE_CFG              BIT(0)	/* WOL TYPE Config */
#define YTPHY_WOL_FEATURE_ENABLE_CFG            BIT(3)	/* WOL Enable Config */
#define YTPHY_WOL_FEATURE_INTR_SEL_CFG          BIT(6)	/* WOL Event Interrupt Enable Config */
#define YTPHY_WOL_FEATURE_WIDTH1_CFG            BIT(1)	/* WOL Pulse Width Config */
#define YTPHY_WOL_FEATURE_WIDTH2_CFG            BIT(2)	/* WOL Pulse Width Config */

#define YTPHY_REG_SPACE_UTP             0
#define YTPHY_REG_SPACE_FIBER           2

enum ytphy_wol_feature_trigger_type_e {
	YTPHY_WOL_FEATURE_PULSE_TRIGGER,
	YTPHY_WOL_FEATURE_LEVEL_TRIGGER,
	YTPHY_WOL_FEATURE_TRIGGER_TYPE_MAX
};

enum ytphy_wol_feature_pulse_width_e {
	YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_336MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_84MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_PULSE_WIDTH_MAX
};

struct ytphy_wol_feature_cfg {
	bool enable;
	int type;
	int width;
};

struct ytphy_chip_info_s {
	int hw_strap_mode;
	unsigned int status_polling_mode;
	unsigned int phy_addr;
	int link_mode;
	int mphy_link_mode[8];	//for multiple port phy, yt8614/8618, max number of port is 8
};

/* MOTORCOMM_PHY_H */
 
#if (YTPHY_WOL_FEATURE_ENABLE)
#undef SYS_WAKEUP_BASED_ON_ETH_PKT
#define SYS_WAKEUP_BASED_ON_ETH_PKT 	1
#endif

/* workaround for 8521 fiber 100m mode */

/* 0: no link; 1: utp; 32: fiber. traced that 1000m fiber uses 32 */
#define YTPHY_FIBER_MODE_SPECIAL_FLAG 	32

/*
 * ext reg 0xa001 contains hw strap configuration of 8521, it can be:
 * 3'b000: UTP_TO_RGMII;
 * 3'b001: FIBER_TO_RGMII;
 * 3'b010: UTP_FIBER_TO_RGMII;
 * 3'b011: UTP_TO_SGMII;
 * 3'b100: SGPHY_TO_RGMAC;
 * 3'b101: SGMAC_TO_RGPHY;
 * 3'b110: UTP_TO_FIBER_AUTO;
 * 3'b111: UTP_TO_FIBER_FORCE
 *
 */

/**************  YT8521    *****************************************/
/* the number of yt8521 present in the board: hw_chip_index_8521+1 */
static int hw_chip_index_8521 = 0xff;
static struct ytphy_chip_info_s yt8521_chip_info[YTPHY_BOARD_MAX_NUM_OF_CHIP_8521];

/* YT8521 polling mode */
#define YT8521_PHY_MODE_FIBER           1 //fiber mode only
#define YT8521_PHY_MODE_UTP             2 //utp mode only
#define YT8521_PHY_MODE_POLL            3 //fiber and utp, poll mode

/* below are for bitmap */
#define YT_PHY_MODE_FIBER		1 //fiber/sgmii mode only
#define YT_PHY_MODE_UTP			2 //utp mode only
#define YT_PHY_MODE_QSGMII		4 //qsgmii mode only
#define YT_PHY_MODE_POLL		(YT_PHY_MODE_FIBER | YT_PHY_MODE_UTP | YT_PHY_MODE_QSGMII) //asgmii, fiber/sgmii and utp, poll mode

/* support automatically check polling mode for yt8521
 * for Fiber only system, please define YT8521_PHY_MODE_CURR 1
 * for UTP only system, please define YT8521_PHY_MODE_CURR 2
 * for combo system, please define YT8521_PHY_MODE_CURR 3
 */
static int yt8521_polling_mode_get(struct phy_device *phydev);
static int yt8614_polling_mode_get(struct phy_device *phydev);

#define YT8521_PHY_MODE_CURR	yt8521_polling_mode_get(phydev)
#define YT8614_PHY_MODE_CURR	yt8614_polling_mode_get(phydev)

/**************  YT8614    *****************************************/
/* the number of yt8614 present in the board: hw_chip_index_8614+1 */
static int hw_chip_index_8614 = 0xff;
static struct ytphy_chip_info_s yt8614_chip_info[YTPHY_BOARD_MAX_NUM_OF_CHIP_8614];
#define YT8614_NUMBER_OF_PORT_PER_CHIP	4

/**************  YT8618    *****************************************/
/* for multiple port phy, base phy address */
static unsigned int yt_mport_base_phy_addr = 0xff; //0xff: invalid; for 8618

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
static int ytphy_config_init(struct phy_device *phydev)
{
	int val;

	val = phy_read(phydev, 3);
	netdev_info(phydev->attached_dev, "ytphy_config_init done for phy id 0x%x.\n", val);
	return 0;
}
#endif

static int ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;
	int val;

	ret = phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, REG_DEBUG_DATA);

	return val;
}

static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, REG_DEBUG_DATA, val);

	return ret;
}

#if (YTPHY8531A_XTAL_INIT)
static int yt8531a_xtal_init(struct phy_device *phydev)
{
	int ret = 0;
	int val = 0;
	static bool state = false;

	if (!state)
	{
		msleep(50);

		do
		{
			ret = ytphy_write_ext(phydev, 0xa012, 0x88);
			if (ret < 0)
				return ret;
			
			msleep(100);

			val = ytphy_read_ext(phydev, 0xa012);
			if (val < 0)
				return val;

			msleep(10);
		}while(0x88 != val);
		
		ret = ytphy_write_ext(phydev, 0xa012, 0xc8);
		if (ret < 0)
			return ret;
		
		state = !state;
	}

	return ret;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
int yt8010_soft_reset(struct phy_device *phydev)
{
	int ret = 0;

	ret = ytphy_write_ext(phydev, 0x0, 0x9000);
	if (ret < 0)
		return ret;

	return 0;
}
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
int yt8010_aneg_done(struct phy_device *phydev)
{
	int val = 0;
	
	val = phy_read(phydev, 0x1);
	
	return (val < 0) ? val : (val & BMSR_LSTATUS);
}
#endif

static int yt8010_config_aneg(struct phy_device *phydev)
{
	phydev->speed = SPEED_100;
	return 0;
}

static int yt8010_read_status(struct phy_device *phydev)
{
	int ret = 0;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	/* for 8010, no definition mii reg 0x04, 0x11, here force 100/full */
	phydev->speed = SPEED_100;
	phydev->duplex = DUPLEX_FULL;

	return 0;
}


#if YT8512_CLOCK_INIT_NEED
static int yt8512_clk_init(struct phy_device *phydev)
{
	int ret;
	int val;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_AFE_PLL);
	if (val < 0)
		return val;

	val |= YT8512_CONFIG_PLL_REFCLK_SEL_EN;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_AFE_PLL, val);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_EXTEND_COMBO);
	if (val < 0)
		return val;

	val |= YT8512_CONTROL1_RMII_EN;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_EXTEND_COMBO, val);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	val |= YT_SOFTWARE_RESET;
	ret = phy_write(phydev, MII_BMCR, val);

	return ret;
}
#endif

static int yt8512_led_init(struct phy_device *phydev)
{
	int ret;
	int val;
	int mask;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED0);
	if (val < 0)
		return val;

	val |= YT8512_LED0_ACT_BLK_IND;

	mask = YT8512_LED0_DIS_LED_AN_TRY | YT8512_LED0_BT_BLK_EN |
		YT8512_LED0_HT_BLK_EN | YT8512_LED0_COL_BLK_EN |
		YT8512_LED0_BT_ON_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_LED0, val);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED1);
	if (val < 0)
		return val;

	val |= YT8512_LED1_BT_ON_EN;

	mask = YT8512_LED1_TXACT_BLK_EN | YT8512_LED1_RXACT_BLK_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_LED1_BT_ON_EN, val);

	return ret;
}

static int yt8512_config_init(struct phy_device *phydev)
{
	int ret;
	int val;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

#if YT8512_CLOCK_INIT_NEED
	ret = yt8512_clk_init(phydev);
	if (ret < 0)
		return ret;
#endif 

	ret = yt8512_led_init(phydev);

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8512_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	return ret;
}

static int yt8512_read_status(struct phy_device *phydev)
{
	int ret;
	int val;
	int speed, speed_mode, duplex;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	duplex = (val & YT8512_DUPLEX) >> YT8512_DUPLEX_BIT;
	speed_mode = (val & YT8512_SPEED_MODE) >> YT8512_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
	case 3:
	default:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
int yt8521_soft_reset(struct phy_device *phydev)
{
	int ret, val;

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 0);
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 2);
		ret = genphy_soft_reset(phydev);

		ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;
	}

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_POLL) {
		val = ytphy_read_ext(phydev, 0xa001);
		ytphy_write_ext(phydev, 0xa001, (val & ~0x8000));

		ytphy_write_ext(phydev, 0xa000, 0);
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

#endif

#if GMAC_CLOCK_INPUT_NEEDED
static int ytphy_mii_rd_ext(struct mii_bus *bus, int phy_id, u32 regnum)
{
	int ret;
	int val;

	ret = bus->write(bus, phy_id, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	val = bus->read(bus, phy_id, REG_DEBUG_DATA);

	return val;
}

static int ytphy_mii_wr_ext(struct mii_bus *bus,
			    int phy_id,
			    u32 regnum,
			    u16 val)
{
	int ret;

	ret = bus->write(bus, phy_id, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	ret = bus->write(bus, phy_id, REG_DEBUG_DATA, val);

	return ret;
}

int yt8511_config_dis_txdelay(struct mii_bus *bus, int phy_id)
{
	int ret;
	int val;

	/* disable auto sleep */
	val = ytphy_mii_rd_ext(bus, phy_id, 0x27);
	if (val < 0)
		return val;

	val &= (~BIT(15));

	ret = ytphy_mii_wr_ext(bus, phy_id, 0x27, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_mii_rd_ext(bus, phy_id, 0xc);
	if (val < 0)
		return val;

	/* ext reg 0xc b[7:4]
	 * Tx Delay time = 150ps * N - 250ps
	 */
	val &= ~(0xf << 4);
	ret = ytphy_mii_wr_ext(bus, phy_id, 0xc, val);

	return ret;
}

int yt8511_config_out_125m(struct mii_bus *bus, int phy_id)
{
	int ret;
	int val;

	/* disable auto sleep */
	val = ytphy_mii_rd_ext(bus, phy_id, 0x27);
	if (val < 0)
		return val;

	val &= (~BIT(15));

	ret = ytphy_mii_wr_ext(bus, phy_id, 0x27, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_mii_rd_ext(bus, phy_id, 0xc);
	if (val < 0)
		return val;

	/* ext reg 0xc.b[2:1]
	 * 00-----25M from pll;
	 * 01---- 25M from xtl;(default)
	 * 10-----62.5M from pll;
	 * 11----125M from pll(here set to this value)
	 */
	val |= (3 << 1);
	ret = ytphy_mii_wr_ext(bus, phy_id, 0xc, val);

#ifdef YT_8511_INIT_TO_MASTER
	/* for customer, please enable it based on demand.
	 * configure to master
	 */

	/* master/slave config reg*/
	val = bus->read(bus, phy_id, 0x9);
	/* to be manual config and force to be master */
	val |= (0x3<<11);
	/* take effect until phy soft reset */
	ret = bus->write(bus, phy_id, 0x9, val);
	if (ret < 0)
		return ret;
#endif

    return ret;
}

EXPORT_SYMBOL(yt8511_config_out_125m);

static int yt8511_config_init(struct phy_device *phydev)
{
	int ret;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif

	netdev_info(phydev->attached_dev, "yt8511_config_init done.\n");
	return ret;
}
#endif /* GMAC_CLOCK_INPUT_NEEDED */

#if (YTPHY_WOL_FEATURE_ENABLE)
static int ytphy_switch_reg_space(struct phy_device *phydev, int space)
{
	int ret;

	if (space == YTPHY_REG_SPACE_UTP)
		ret = ytphy_write_ext(phydev, 0xa000, 0);
	else
		ret = ytphy_write_ext(phydev, 0xa000, 2);

	return ret;
}

static int ytphy_wol_feature_enable_cfg(struct phy_device *phydev,
			    struct ytphy_wol_feature_cfg wol_cfg)
{
	int ret = 0;
	int val = 0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG);
	if (val < 0)
		return val;

	if (wol_cfg.enable) {
		val |= YTPHY_WOL_FEATURE_ENABLE_CFG;

		if (wol_cfg.type == YTPHY_WOL_FEATURE_LEVEL_TRIGGER) {
			val &= ~YTPHY_WOL_FEATURE_TYPE_CFG;
			val &= ~YTPHY_WOL_FEATURE_INTR_SEL_CFG;
		} else if (wol_cfg.type == YTPHY_WOL_FEATURE_PULSE_TRIGGER) {
			val |= YTPHY_WOL_FEATURE_TYPE_CFG;
			val |= YTPHY_WOL_FEATURE_INTR_SEL_CFG;

			if (wol_cfg.width == YTPHY_WOL_FEATURE_84MS_PULSE_WIDTH) {
				val &= ~YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val &= ~YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width == YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH) {
				val |= YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val &= ~YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width == YTPHY_WOL_FEATURE_336MS_PULSE_WIDTH) {
				val &= ~YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val |= YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width == YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH) {
				val |= YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val |= YTPHY_WOL_FEATURE_WIDTH2_CFG;
			}
		}
	} else {
		val &= ~YTPHY_WOL_FEATURE_ENABLE_CFG;
		val &= ~YTPHY_WOL_FEATURE_INTR_SEL_CFG;
	}

	ret = ytphy_write_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG, val);
	if (ret < 0)
		return ret;

	return 0;
}

static void ytphy_wol_feature_get(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int val = 0;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG);
	if (val < 0)
		return;

	if (val & YTPHY_WOL_FEATURE_ENABLE_CFG)
		wol->wolopts |= WAKE_MAGIC;

	return;
}

static int ytphy_wol_feature_set(struct phy_device *phydev,
			 struct ethtool_wolinfo *wol)
{
	int ret, curr_reg_space, val;
	struct ytphy_wol_feature_cfg wol_cfg;
	struct net_device *p_attached_dev = phydev->attached_dev;

	memset(&wol_cfg, 0, sizeof(struct ytphy_wol_feature_cfg));
	curr_reg_space = ytphy_read_ext(phydev, 0xa000);
	if (curr_reg_space < 0)
		return curr_reg_space;

	/* Switch to phy UTP page */
	ret = ytphy_switch_reg_space(phydev, YTPHY_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Enable the WOL feature interrupt */
		val = phy_read(phydev, YTPHY_UTP_INTR_REG);
		val |= YTPHY_WOL_FEATURE_INTR;
		ret = phy_write(phydev, YTPHY_UTP_INTR_REG, val);
		if (ret < 0)
			return ret;

		/* Set the WOL feature config */
		wol_cfg.enable = true;
		wol_cfg.type = YTPHY_WOL_FEATURE_PULSE_TRIGGER;
		wol_cfg.width = YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH;
		ret = ytphy_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;

		/* Store the device address for the magic packet */
		ret = ytphy_write_ext(phydev, YTPHY_WOL_FEATURE_MACADDR2_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[0] << 8) |
					p_attached_dev->dev_addr[1]));
		if (ret < 0)
			return ret;
		ret = ytphy_write_ext(phydev, YTPHY_WOL_FEATURE_MACADDR1_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[2] << 8) |
				       p_attached_dev->dev_addr[3]));
		if (ret < 0)
			return ret;
		ret = ytphy_write_ext(phydev, YTPHY_WOL_FEATURE_MACADDR0_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[4] << 8) |
				       p_attached_dev->dev_addr[5]));
		if (ret < 0)
			return ret;
	} else {
		wol_cfg.enable = false;
		wol_cfg.type = YTPHY_WOL_FEATURE_TRIGGER_TYPE_MAX;
		wol_cfg.width = YTPHY_WOL_FEATURE_PULSE_WIDTH_MAX;
		ret = ytphy_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;
	}

	/* Recover to previous register space page */
	ret = ytphy_switch_reg_space(phydev, curr_reg_space);
	if (ret < 0)
		return ret;

	return 0;
}

#endif /*(YTPHY_WOL_FEATURE_ENABLE)*/

/* NOTE: !!!
 * this function should be strictly called once for each 8521 chip 
 */
static int yt8521_hw_strap_cfg_get(struct phy_device *phydev)
{
	int val, i;
	int new_phy;

	new_phy = 1;
	if(hw_chip_index_8521 == 0xff) {
		/* the first chip of yt8521 */
		hw_chip_index_8521 = 0;
	} else {
	
		/* check if the phy addr exists */
		for (i = 0; i <= hw_chip_index_8521; i++)
		{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->addr)
#else
			if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->mdio.addr)
#endif
			{
				new_phy = 0;
				return yt8521_chip_info[i].hw_strap_mode;
			}
		}

		if(new_phy) {
			if(++hw_chip_index_8521 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
				netdev_info(phydev->attached_dev, "Chip num of yt8521 reachess max limit, present %d, limit %d.\n", hw_chip_index_8521, YTPHY_BOARD_MAX_NUM_OF_CHIP_8521);
				hw_chip_index_8521 = (YTPHY_BOARD_MAX_NUM_OF_CHIP_8521 - 1);
			}
		}
	}

	val = ytphy_read_ext(phydev, 0xa001) & 0x7;
	netdev_info(phydev->attached_dev, "yt8521 hw strap config mode=%d, chip idx=%d.\n", val, hw_chip_index_8521);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	yt8521_chip_info[hw_chip_index_8521].phy_addr = (unsigned int)phydev->addr;
#else
	yt8521_chip_info[hw_chip_index_8521].phy_addr = (unsigned int)phydev->mdio.addr;
#endif

	switch (val) {
	case 1:
	case 4:
	case 5:
		yt8521_chip_info[hw_chip_index_8521].status_polling_mode = YT8521_PHY_MODE_FIBER;
		break;
	case 2:
	case 6:
	case 7:
		yt8521_chip_info[hw_chip_index_8521].status_polling_mode = YT8521_PHY_MODE_POLL;
		break;
	case 3:
	case 0:
	default:
		yt8521_chip_info[hw_chip_index_8521].status_polling_mode = YT8521_PHY_MODE_UTP;
	}
	yt8521_chip_info[hw_chip_index_8521].hw_strap_mode = val;

	return yt8521_chip_info[hw_chip_index_8521].hw_strap_mode;
}

static int yt8521_polling_mode_get(struct phy_device *phydev)
{
	int i;

	if(hw_chip_index_8521 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
		netdev_info(phydev->attached_dev, "yt8521 polling mode get before stap checking, default polling mode.\n");
		return YT8521_PHY_MODE_UTP;
	}
	
	for (i = 0; i <= hw_chip_index_8521; i++)
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->addr)
#else
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->mdio.addr)
#endif
		{
			return yt8521_chip_info[i].status_polling_mode;
		}

	}

	netdev_info(phydev->attached_dev, "yt8521 polling mode get failed, no matched phy addr.\n");
	return YT8521_PHY_MODE_POLL;
}

static int yt8521_link_mode_get(struct phy_device *phydev)
{
	int i;

	if(hw_chip_index_8521 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
		netdev_info(phydev->attached_dev, "yt8521 link mode get before status checking, default link mode.\n");
		return 0;
	}
	
	for (i = 0; i <= hw_chip_index_8521; i++)
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->addr)
#else
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->mdio.addr)
#endif
		{
			return yt8521_chip_info[i].link_mode;
		}
	}

	netdev_info(phydev->attached_dev, "yt8521 link mode get failed, no matched phy addr.\n");
	return 0;
}

static int yt8521_link_mode_set(struct phy_device *phydev, int lmode)
{
	int i;

	if(hw_chip_index_8521 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
		netdev_info(phydev->attached_dev, "yt8521 link mode set before initialization.\n");
		return 1;
	}
	
	for (i = 0; i <= hw_chip_index_8521; i++)
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->addr)
#else
		if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->mdio.addr)
#endif
		{
			yt8521_chip_info[i].link_mode = lmode;
			return 0;
		}
	}

	netdev_info(phydev->attached_dev, "yt8521 link mode set failed, no matched phy addr.\n");
	return 1;
}

static int yt8521_config_init(struct phy_device *phydev)
{
	int ret;
	int val, hw_strap_mode;

#if (YTPHY_WOL_FEATURE_ENABLE)
	struct ethtool_wolinfo wol;

	/* set phy wol enable */
	memset(&wol, 0x0, sizeof(struct ethtool_wolinfo));
	wol.wolopts |= WAKE_MAGIC;
	ytphy_wol_feature_set(phydev, &wol);
#endif

	phydev->irq = PHY_POLL;
	/* NOTE: this function should not be called more than one for each chip. */
	hw_strap_mode = yt8521_hw_strap_cfg_get(phydev);

	ytphy_write_ext(phydev, 0xa000, 0);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_read_ext(phydev, 0xc);
	if (val < 0)
		return val;
	val &= ~(1 << 12);
	ret = ytphy_write_ext(phydev, 0xc, val);
	if (ret < 0)
		return ret;

	netdev_info(phydev->attached_dev, "yt8521 config init done, strap mode=%d, polling mode=%d.\n", hw_strap_mode, yt8521_polling_mode_get(phydev));
	return ret;
}

/* for fiber mode, there is no 10M speed mode and
 * this function is for this purpose.
 */
static int yt8521_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed_mode, duplex;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	int speed = -1;
#else
	int speed = SPEED_UNKNOWN;
#endif

	if (is_utp)
		duplex = (val & YT8512_DUPLEX) >> YT8521_DUPLEX_BIT;
	else
		duplex = 1;
	speed_mode = (val & YT8521_SPEED_MODE) >> YT8521_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		if (is_utp)
			speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 3:
		break;
	default:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

/* for fiber mode, when speed is 100M, there is no definition for
 * autonegotiation, and this function handles this case and return
 * 1 per linux kernel's polling.
 */
int yt8521_aneg_done(struct phy_device *phydev)
{
	if ((yt8521_link_mode_get(phydev) == YTPHY_FIBER_MODE_SPECIAL_FLAG) && (phydev->speed == SPEED_100))
		return 1; /*link_mode_8521*/

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
	return genphy_aneg_done(phydev);
#else
	return 1;
#endif
}

static int yt8521_read_status(struct phy_device *phydev)
{
	int ret;
	volatile int val;
	volatile int yt8521_fiber_latch_val;
	volatile int yt8521_fiber_curr_val;
	volatile int link;
	int link_utp = 0, link_fiber = 0;

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		/* reading UTP */
		ret = ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link) {
			link_utp = 1;
			yt8521_link_mode_set(phydev, 1);
			yt8521_adjust_status(phydev, val, 1);
		} else {
			link_utp = 0;
		}
	} //(YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER)

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP) {
		/* reading Fiber */
		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		//note: below debug information is used to check multiple PHy ports.

		/* for fiber, from 1000m to 100m, there is not link down from 0x11,
		 * and check reg 1 to identify such case this is important for Linux
		 * kernel for that, missing linkdown event will cause problem.
		 */
		yt8521_fiber_latch_val = phy_read(phydev, MII_BMSR);
		yt8521_fiber_curr_val = phy_read(phydev, MII_BMSR);
		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link && yt8521_fiber_latch_val != yt8521_fiber_curr_val) {
			link = 0;
			netdev_info(phydev->attached_dev, "yt8521, fiber link down detect,latch=%04x,curr=%04x\n",
			yt8521_fiber_latch_val, yt8521_fiber_curr_val);
		}

		if (link) {
			link_fiber = 1;
			yt8521_adjust_status(phydev, val, 0);
			yt8521_link_mode_set(phydev, YTPHY_FIBER_MODE_SPECIAL_FLAG);
		} else {
			link_fiber = 0;
		}
	} //(YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP)

	if (link_utp || link_fiber) {
		if(phydev->link == 0)
			netdev_info(phydev->attached_dev, "yt8521, link up, media %s, reg 0x11=0x%x.\n",
				(link_utp && link_fiber) ? "both UTP and Fiber" : (link_utp ? "UTP" : "Fiber"), (unsigned int)val);
		phydev->link = 1;
	} else {
		if(phydev->link == 1)
			netdev_info(phydev->attached_dev, "yt8521, link down.\n");
		phydev->link = 0;
		yt8521_link_mode_set(phydev, 0);
	}

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		if (link_utp)
			ytphy_write_ext(phydev, 0xa000, 0);
	}
	return 0;
}

int yt8521_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock in 4.19 */
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 2);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 0);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

int yt8521_resume(struct phy_device *phydev)
{
	int value, ret;

	/* disable auto sleep */
	value = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (value < 0)
		return value;

	value &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, value);
	if (ret < 0)
		return ret;

#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 0);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);
	}

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 2);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

		ytphy_write_ext(phydev, 0xa000, 0);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8531S_config_init(struct phy_device *phydev)
{
	int ret = 0;

#if (YTPHY8531A_XTAL_INIT)
	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif

	ret = yt8521_config_init(phydev);

	return ret;
}

static int yt8531_config_init(struct phy_device *phydev)
{
	int ret = 0;

#if (YTPHY8531A_XTAL_INIT)
	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;
	
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
int yt8618_soft_reset(struct phy_device *phydev)
{
	int ret;

	ytphy_write_ext(phydev, 0xa000, 0);
	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

int yt8614_soft_reset(struct phy_device *phydev)
{
	int ret;

	/* qsgmii */
	ytphy_write_ext(phydev, 0xa000, 2);
	ret = genphy_soft_reset(phydev);
	if (ret < 0) {
		ytphy_write_ext(phydev, 0xa000, 0); //back to utp mode
		return ret;
	}

	/* sgmii */
	ytphy_write_ext(phydev, 0xa000, 3);
	ret = genphy_soft_reset(phydev);
	if (ret < 0) {
		ytphy_write_ext(phydev, 0xa000, 0); //back to utp mode
		return ret;
	}

	/* utp */
	ytphy_write_ext(phydev, 0xa000, 0);
	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;	

	return 0;
}

#endif

static int yt8618_config_init(struct phy_device *phydev)
{
	int ret;
	int val;
    unsigned int retries = 12;

	phydev->irq = PHY_POLL;

	if (yt_mport_base_phy_addr == 0xff)
		/* by default, we think the first phy should be the base phy addr. for case of multiply phy  */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
		yt_mport_base_phy_addr = phydev->addr;
	else if (yt_mport_base_phy_addr > phydev->addr)
		netdev_info(phydev->attached_dev, "YT8618 init, phy address mismatch, base=%d, cur=%d\n", yt_mport_base_phy_addr, phydev->addr);
#else
		yt_mport_base_phy_addr = phydev->mdio.addr;
	else if (yt_mport_base_phy_addr > phydev->mdio.addr)
		netdev_info(phydev->attached_dev, "YT8618 init, phy address mismatch, base=%d, cur=%d\n", yt_mport_base_phy_addr, phydev->mdio.addr);
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	/* for utp to optimize signal */
	ret = ytphy_write_ext(phydev, 0x41, 0x33);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x42, 0x66);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x43, 0xaa);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x44, 0xd0d);
	if (ret < 0)
		return ret;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	if (phydev->addr > yt_mport_base_phy_addr &&
	    (phydev->addr - yt_mport_base_phy_addr == 2 ||
	     phydev->addr - yt_mport_base_phy_addr == 5))
#else
	if (phydev->mdio.addr > yt_mport_base_phy_addr &&
	    (phydev->mdio.addr - yt_mport_base_phy_addr == 2 ||
	     phydev->mdio.addr - yt_mport_base_phy_addr == 5))
#endif
	{
		ret = ytphy_write_ext(phydev, 0x57, 0x2929);
		if (ret < 0)
			return ret;
	}

	val = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, val | BMCR_RESET);
	do {
		msleep(50);
		ret = phy_read(phydev, MII_BMCR);
		if (ret < 0)
			return ret;
	} while ((ret & BMCR_RESET) && --retries);
	if (ret & BMCR_RESET)
		return -ETIMEDOUT;

    /* for QSGMII optimization */
    ret = ytphy_write_ext(phydev, 0xa000, 0x02);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0x3, 0x4F80);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0xe, 0x4F80);
    if (ret < 0)
        return ret;

    netdev_info(phydev->attached_dev, "yt8618_config_init done, base PHY addr is %d.\n", yt_mport_base_phy_addr);
    return ret;
}

/* NOTE: !!!
 * this function should be strictly called once for each 8614 chip 
 */
static int yt8614_hw_strap_cfg_get(struct phy_device *phydev)
{
	int val, i, j;
	int new_phy;

	new_phy = 1;
	if(hw_chip_index_8614 == 0xff) {
		/* the first chip of yt8614 */
		hw_chip_index_8614 = 0;
	} else {
	
		/* check if the phy addr exists */
		for (i = 0; i <= hw_chip_index_8614; i++)
		{
			for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
				if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
				if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
				{
					new_phy = 0;
					return yt8614_chip_info[i].hw_strap_mode;
				}
			}
		}

		if(new_phy) {
			if(++hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8614) {
				netdev_info(phydev->attached_dev, "Chip num of yt8614 reachess max limit, present %d, limit %d.\n", hw_chip_index_8614, YTPHY_BOARD_MAX_NUM_OF_CHIP_8614);
				hw_chip_index_8614 = (YTPHY_BOARD_MAX_NUM_OF_CHIP_8614 - 1);
			}
		}
	}

	/* b3:0 are work mode, but b3 is always 1 */
	val = ytphy_read_ext(phydev, 0xa007) & 0xf;
	netdev_info(phydev->attached_dev, "yt8614 hw strap config mode=%d, chip idx=%d.\n", val, hw_chip_index_8614);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	yt8614_chip_info[hw_chip_index_8614].phy_addr = (unsigned int)phydev->addr;
#else
	yt8614_chip_info[hw_chip_index_8614].phy_addr = (unsigned int)phydev->mdio.addr;
#endif

	switch (val) {
	case 8:
	case 12:
	case 13:
	case 15:
		yt8614_chip_info[hw_chip_index_8614].status_polling_mode = YT_PHY_MODE_FIBER | YT_PHY_MODE_UTP;
		break;
	case 14:
	case 11:
		yt8614_chip_info[hw_chip_index_8614].status_polling_mode = YT_PHY_MODE_FIBER;
		break;
	case 9:
	case 10:
	default:
		yt8614_chip_info[hw_chip_index_8614].status_polling_mode = YT_PHY_MODE_UTP;
	}

	if((val > 9 ) && (val < 0xf )) yt8614_chip_info[hw_chip_index_8614].status_polling_mode |= YT_PHY_MODE_QSGMII;
	yt8614_chip_info[hw_chip_index_8614].hw_strap_mode = val;

	return yt8614_chip_info[hw_chip_index_8614].hw_strap_mode;
}

static int yt8614_chip_base_phy_get(struct phy_device *phydev)
{
	int i, j;

	if(hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8614) {
		netdev_info(phydev->attached_dev, "yt8614 chip base phy get before stap checking.\n");

		/* invlid phy addr */
		return 0xff;
	}
	
	for (i = 0; i <= hw_chip_index_8614; i++)
	{

		for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
			{
				return yt8614_chip_info[i].phy_addr;
			}
		}
	}

	netdev_info(phydev->attached_dev, "yt8614 chip base phy get failed, no matched phy addr.\n");
	return 0xff;
}

static int yt8614_get_port_from_phydev(struct phy_device *phydev)
{
	int i, j;

	if(hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8614) {
		netdev_info(phydev->attached_dev, "yt8614 get port before stap checking.\n");

		return 0;
	}
	
	for (i = 0; i <= hw_chip_index_8614; i++)
	{
		for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
			{
				return j;
			}
		}
	}

	netdev_info(phydev->attached_dev, "yt8614 get port failed, no matched phy addr.\n");
	return 0;
}

static int yt8614_polling_mode_get(struct phy_device *phydev)
{
	int i, j;

	if(hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8614) {
		netdev_info(phydev->attached_dev, "yt8614 polling mode get before stap checking, default polling mode.\n");
		return YT_PHY_MODE_UTP;
	}
	
	for (i = 0; i <= hw_chip_index_8614; i++)
	{

		for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
			{
				return yt8614_chip_info[i].status_polling_mode;
			}
		}
	}

	netdev_info(phydev->attached_dev, "yt8614 polling mode get failed, no matched phy addr.\n");
	return YT_PHY_MODE_UTP;
}

static int yt8614_link_mode_get(struct phy_device *phydev)
{
	int i, j;

	if(hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8614) {
		netdev_info(phydev->attached_dev, "yt8614 link mode get before status checking, default link mode.\n");
		return 0;
	}
	
	for (i = 0; i <= hw_chip_index_8614; i++)
	{
		for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
			{
				return yt8614_chip_info[i].mphy_link_mode[j];
			}
		}
	}

	netdev_info(phydev->attached_dev, "yt8614 link mode get failed, no matched phy addr.\n");
	return 0;
}

static int yt8614_link_mode_set(struct phy_device *phydev, int lmode)
{
	int i, j;

	if(hw_chip_index_8614 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
		netdev_info(phydev->attached_dev, "yt8614 link mode set before initialization.\n");
		return 1;
	}
	
	for (i = 0; i <= hw_chip_index_8614; i++)
	{
		for(j = 0; j < YT8614_NUMBER_OF_PORT_PER_CHIP; j++) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->addr)
#else
			if((yt8614_chip_info[i].phy_addr + j) == (unsigned int)phydev->mdio.addr)
#endif
			{
				yt8614_chip_info[i].mphy_link_mode[j] = lmode;
				return 0;
			}
		}
	}

	netdev_info(phydev->attached_dev, "yt8614 link mode set failed, no matched phy addr.\n");
	return 1;
}


static int yt8614_config_init(struct phy_device *phydev)
{
	int port, ret = 0;
	int val, hw_strap_mode;
    unsigned int retries = 12;

	phydev->irq = PHY_POLL;

	/* NOTE: this function should not be called more than one for each chip. */
	hw_strap_mode = yt8614_hw_strap_cfg_get(phydev);

	port = yt8614_get_port_from_phydev(phydev);

	ytphy_write_ext(phydev, 0xa000, 0);
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	/* for utp to optimize signal */
	ret = ytphy_write_ext(phydev, 0x41, 0x33);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x42, 0x66);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x43, 0xaa);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x44, 0xd0d);
	if (ret < 0)
		return ret;
	if (port == 2)
	{
		ret = ytphy_write_ext(phydev, 0x57, 0x2929);
		if (ret < 0)
			return ret;
	}

	/* soft reset to take config effect */
	val = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, val | BMCR_RESET);
	do {
		msleep(50);
		ret = phy_read(phydev, MII_BMCR);
		if (ret < 0)
			return ret;
	} while ((ret & BMCR_RESET) && --retries);
	if (ret & BMCR_RESET)
		return -ETIMEDOUT;

    /* for QSGMII optimization */
    ret = ytphy_write_ext(phydev, 0xa000, 0x02);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0x3, 0x4F80);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0xe, 0x4F80);
    if (ret < 0)
        return ret;

    /* for SGMII optimization */
    ret = ytphy_write_ext(phydev, 0xa000, 0x03);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0x3, 0x2420);
    if (ret < 0)
        return ret;
    ret = ytphy_write_ext(phydev, 0xe, 0x24a0);
    if (ret < 0)
        return ret;

    netdev_info(phydev->attached_dev, "yt8614_config_init done, base PHY addr is %d (strap=%d), curr port=%d.\n", yt8614_chip_base_phy_get(phydev), hw_strap_mode, port);
    return ret;
}


int yt8618_aneg_done (struct phy_device *phydev)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
	return genphy_aneg_done(phydev);
#else
	return 1;
#endif
}

int yt8614_aneg_done (struct phy_device *phydev)
{
	/*it should be used for 8614 fiber*/
	if (yt8614_link_mode_get(phydev) == YTPHY_FIBER_MODE_SPECIAL_FLAG && phydev->speed == SPEED_100)
		return 1;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
	return genphy_aneg_done(phydev);
#else
	return 1;
#endif

}

static int yt8614_read_status(struct phy_device *phydev)
{
	int ret;
	volatile int val, yt8614_fiber_latch_val, yt8614_fiber_curr_val;
	volatile int link;
	int link_utp = 0, link_fiber = 0/* , link_qsgmii = 0 */;
	int port = yt8614_get_port_from_phydev(phydev);
	//unsigned int base_phyaddr = yt8614_chip_base_phy_get(phydev);

	if (YT8614_PHY_MODE_CURR & YT_PHY_MODE_UTP) {
		/* switch to utp and reading regs  */
		ret = ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link) {
			if(phydev->link == 0)
				netdev_info(phydev->attached_dev, "yt8614 port %d link up, UTP, reg 0x11=0x%x.\n", port, (unsigned int)val);
			link_utp = 1;
			// here is same as 8521 and re-use the function;
			yt8521_adjust_status(phydev, val, 1);
		} else {
			link_utp = 0;
		}
	}
	
	if (YT8614_PHY_MODE_CURR & YT_PHY_MODE_FIBER) {
		/* reading Fiber/sgmii */
		ret = ytphy_write_ext(phydev, 0xa000, 3);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		/* for fiber, from 1000m to 100m, there is not link down from 0x11,
		 * and check reg 1 to identify such case
		 */
		yt8614_fiber_latch_val = phy_read(phydev, MII_BMSR);
		yt8614_fiber_curr_val = phy_read(phydev, MII_BMSR);
		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link && yt8614_fiber_latch_val != yt8614_fiber_curr_val) {
			link = 0;
			netdev_info(phydev->attached_dev, "yt8614_read_status, port %d fiber link down detect, latch=%04x,curr=%04x\n", port, yt8614_fiber_latch_val, yt8614_fiber_curr_val);
		}

		if (link) {
			if(phydev->link == 0)
				netdev_info(phydev->attached_dev, "yt8614 port %d link up, Fiber, reg 0x11=0x%x.\n", port, (unsigned int)val);
			link_fiber = 1;
			yt8521_adjust_status(phydev, val, 0);
			yt8614_link_mode_set(phydev, YTPHY_FIBER_MODE_SPECIAL_FLAG); //fiber mode
		} else {
			link_fiber = 0;
		}
	}


	if (link_utp || link_fiber) {
		if(phydev->link == 0)
			netdev_info(phydev->attached_dev, "yt8614 port %d link up, media %s.\n", port, (link_utp && link_fiber) ? "both UTP and Fiber" : (link_utp ? "UTP" : "Fiber"));
		phydev->link = 1;
	} else {
		if(phydev->link == 1)
			netdev_info(phydev->attached_dev, "yt8614 port %d link down\n", port);
		phydev->link = 0;
		yt8614_link_mode_set(phydev, 0);
	}

	if (YT8614_PHY_MODE_CURR & YT_PHY_MODE_UTP) {
		if (link_utp)
			ytphy_write_ext(phydev, 0xa000, 0);
	}
	return 0;
}


static int yt8618_read_status(struct phy_device *phydev)
{
	int ret;

	/* maybe for 8614 yt8521_fiber_latch_val, yt8521_fiber_curr_val; */
	volatile int val;
	volatile int link;
	int link_utp = 0, link_fiber = 0;

	/* switch to utp and reading regs  */
	ret = ytphy_write_ext(phydev, 0xa000, 0);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	link = val & (BIT(YT8521_LINK_STATUS_BIT));
	if (link) {
		link_utp = 1;
		yt8521_adjust_status(phydev, val, 1);
	} else {
		link_utp = 0;
	}

	if (link_utp || link_fiber)
		phydev->link = 1;
	else
		phydev->link = 0;

	return 0;
}

int yt8618_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock in 4.19 */
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

int yt8618_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

int yt8614_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock in 4.19 */
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 3);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 0);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

int yt8614_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_lock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 3);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 0);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif /* !(SYS_WAKEUP_BASED_ON_ETH_PKT) */

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
int yt8821_soft_reset(struct phy_device *phydev)
{
	int ret, val;
    
	val = ytphy_read_ext(phydev, 0xa001);
	ytphy_write_ext(phydev, 0xa001, (val & ~0x8000));

	ytphy_write_ext(phydev, 0xa000, 0);
	ret = genphy_soft_reset(phydev);
	
	return ret;
}
#endif

/* NOTE: !!!
 * this function should be strictly called once for each 8821 chip 
 */
static int yt8821_hw_strap_cfg_get(struct phy_device *phydev)
{
	int val, i;
	int new_phy;

	new_phy = 1;
	if(hw_chip_index_8521 == 0xff) {
		/* the first chip of yt8521 */
		hw_chip_index_8521 = 0;
	} else {
	
		/* check if the phy addr exists */
		for (i = 0; i <= hw_chip_index_8521; i++)
		{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
			if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->addr)
#else
			if(yt8521_chip_info[i].phy_addr == (unsigned int)phydev->mdio.addr)
#endif
			{
				new_phy = 0;
				return yt8521_chip_info[i].hw_strap_mode;
			}
		}

		if(new_phy) {
			if(++hw_chip_index_8521 >= YTPHY_BOARD_MAX_NUM_OF_CHIP_8521) {
				netdev_info(phydev->attached_dev, "Chip num of yt8821 reachess max limit, present %d, limit %d.\n", hw_chip_index_8521, YTPHY_BOARD_MAX_NUM_OF_CHIP_8521);
				hw_chip_index_8521 = (YTPHY_BOARD_MAX_NUM_OF_CHIP_8521 - 1);
			}
		}
	}

	val = ytphy_read_ext(phydev, 0xa001) & 0x7;
	netdev_info(phydev->attached_dev, "yt8821 hw strap config mode=%d, chip idx=%d.\n", val, hw_chip_index_8521);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	yt8521_chip_info[hw_chip_index_8521].phy_addr = (unsigned int)phydev->addr;
#else
	yt8521_chip_info[hw_chip_index_8521].phy_addr = (unsigned int)phydev->mdio.addr;
#endif

	switch (val) {
	case 0:
	case 1:
	case 2:
	case 5:
	default:
		yt8521_chip_info[hw_chip_index_8521].status_polling_mode = YT8521_PHY_MODE_POLL;
		break;
	}
	yt8521_chip_info[hw_chip_index_8521].hw_strap_mode = val;

	return yt8521_chip_info[hw_chip_index_8521].hw_strap_mode;
}

static int yt8821_init(struct phy_device *phydev)
{
	int ret = 0;
	int val = 0;
	
	/* sds pll cfg */
	ret = ytphy_write_ext(phydev, 0xa050, 0x1000);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa000, 0x2);	
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x23, 0x47a1);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xbd, 0x3547);
	if (ret < 0)
		return ret;

	/* wait 1s */
	msleep(1000);
    
	/* calibration dcc */
	ret = ytphy_write_ext(phydev, 0xbd, 0xa547);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x29, 0x3003);
	if (ret < 0)
		return ret;

	/* sds driver swing */
	ret = ytphy_write_ext(phydev, 0x25, 0x788);
	if (ret < 0)
		return ret;
	
	/* phy cfg */
	ret = ytphy_write_ext(phydev, 0xa000, 0x0);
	if (ret < 0)
		return ret;

	/* phy template cfg */
	ret = ytphy_write_ext(phydev, 0x471, 0x4545);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x476, 0x4848);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x477, 0x4848);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x478, 0x4848);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x479, 0x4848);
	if (ret < 0)
		return ret;
	
	/* calibrate phy lc pll */
	ret = ytphy_write_ext(phydev, 0x600, 0x2300);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x8, 0x8041);
	if (ret < 0)
		return ret;

	/* prm_small_lng/med */
	ret = ytphy_write_ext(phydev, 0x388, 0x90);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x387, 0x90);
	if (ret < 0)
		return ret;

	/* echo_delay_cfg */
	ret = ytphy_write_ext(phydev, 0x3, 0xa026);
	if (ret < 0)
		return ret;
	
	/* pbo setting */
	ret = ytphy_write_ext(phydev, 0x47e, 0x3535);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x47f, 0x3535);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x480, 0x3535);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x481, 0x3535);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x483, 0x2a2a);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x484, 0x2a2a);	
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x485, 0x2a2a);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x486, 0x2a2a);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x488, 0x2121);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x489, 0x2121);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x48a, 0x2121);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x48b, 0x2121);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x48d, 0x1a1a);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x48e, 0x1a1a);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x48f, 0x1a1a);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x490, 0x1a1a);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x492, 0x1515);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x493, 0x1515);	  
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x494, 0x1515);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x495, 0x1515);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x497, 0x1111);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x498, 0x1111);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x499, 0x1111);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x49a, 0x1111);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x49c, 0x0d0d);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x49d, 0x0d0d);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x49e, 0x0d0d);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x49f, 0x0d0d);	 
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa052, 0x7);
	if (ret < 0)
		return ret;
	
	/* fast link down cfg */
	ret = ytphy_write_ext(phydev, 0x355, 0x7d07);
	if (ret < 0)
		return ret;
	
	/* soft reset */
	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;
	ret = phy_write(phydev, MII_BMCR, val | BMCR_RESET);

    return ret;
}

static int yt8821_config_init(struct phy_device *phydev)
{
	int ret;
	int val, hw_strap_mode;

	phydev->irq = PHY_POLL;

	/* NOTE: this function should not be called more than one for each chip. */
	hw_strap_mode = yt8821_hw_strap_cfg_get(phydev);

	ytphy_write_ext(phydev, 0xa000, 0);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	ret = yt8821_init(phydev);
	if (ret < 0)
		return ret;
    
	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;
	
	netdev_info(phydev->attached_dev, "yt8821 config init done, strap mode=%d.\n", hw_strap_mode);
	return ret;
}

/* for fiber mode, there is no 10M speed mode and
 * this function is for this purpose.
 */
static int yt8821_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed_mode, duplex;
	int speed_mode_bit15_14, speed_mode_bit9;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	int speed = -1;
#else
	int speed = SPEED_UNKNOWN;
#endif

	if (is_utp)
		duplex = (val & YT8512_DUPLEX) >> YT8521_DUPLEX_BIT;
	else
		duplex = 1;

	/* Bit9-Bit15-Bit14 speed mode 100---2.5G; 010---1000M; 001---100M; 000---10M */
	speed_mode_bit15_14 = (val & YT8521_SPEED_MODE) >> YT8521_SPEED_MODE_BIT;
	speed_mode_bit9 = (val & BIT(9)) >> 9;
	speed_mode = (speed_mode_bit9 << 2) | speed_mode_bit15_14; 
	switch (speed_mode) {
	case 0:
		if (is_utp)
			speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 4:
		speed = SPEED_2500;
		break;
	default:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

static int yt8821_read_status(struct phy_device *phydev)
{
	int ret;
	volatile int val;
	volatile int yt8521_fiber_latch_val;
	volatile int yt8521_fiber_curr_val;
	volatile int link;
	static int link_utp = 0, link_fiber = 0;

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		/* reading UTP */
		ret = ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link) {
			if(link_utp == 0)
				netdev_info(phydev->attached_dev, "yt8821, link up, UTP, reg 0x11=0x%x.\n", (unsigned int)val);

			link_utp = 1;
			yt8521_link_mode_set(phydev, 1);
			yt8821_adjust_status(phydev, val, 1);	/* speed(2500), duplex */
		} else {
			link_utp = 0;
		}
	} //(YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER)

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP) {
		/* reading Fiber */
		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		//note: below debug information is used to check multiple PHy ports.

		/* for fiber, from 1000m to 100m, there is not link down from 0x11,
		 * and check reg 1 to identify such case this is important for Linux
		 * kernel for that, missing linkdown event will cause problem.
		 */
		yt8521_fiber_latch_val = phy_read(phydev, MII_BMSR);
		yt8521_fiber_curr_val = phy_read(phydev, MII_BMSR);
		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link && yt8521_fiber_latch_val != yt8521_fiber_curr_val) {
			link = 0;
			netdev_info(phydev->attached_dev, "yt8821, fiber link down detect,latch=%04x,curr=%04x\n",
			yt8521_fiber_latch_val, yt8521_fiber_curr_val);
		}

		if (link) {
			if(link_fiber == 0)
				netdev_info(phydev->attached_dev, "yt8821, link up, Fiber, reg 0x11=0x%x.\n", (unsigned int)val);
			
			link_fiber = 1;
			yt8821_adjust_status(phydev, val, 0);
			yt8521_link_mode_set(phydev, YTPHY_FIBER_MODE_SPECIAL_FLAG);
		} else {
			link_fiber = 0;
		}
	} //(YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP)

	if (link_utp || link_fiber) {
		if(phydev->link == 0)
			netdev_info(phydev->attached_dev, "yt8821, link up, media %s, reg 0x11=0x%x.\n",
				(link_utp && link_fiber) ? "both UTP and Fiber" : (link_utp ? "UTP" : "Fiber"), (unsigned int)val);
		phydev->link = 1;
	} else {
		if(phydev->link == 1)
			netdev_info(phydev->attached_dev, "yt8821, link down.\n");
		phydev->link = 0;
		yt8521_link_mode_set(phydev, 0);
	}

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		if (link_utp)
			ytphy_write_ext(phydev, 0xa000, 0);
	}
	return 0;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 1, 21))
static int yt8821_get_features(struct phy_device *phydev)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->supported, 1);
	return genphy_read_abilities(phydev);
}
#endif

static struct phy_driver ytphy_drvs[] = {
	{
		.phy_id		= PHY_ID_YT8010,
		.name		= "YT8010 Automotive Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset     = yt8010_soft_reset,
#endif
		.config_aneg	= yt8010_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
		.aneg_done		= yt8010_aneg_done,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
		.config_init	= ytphy_config_init,
#else
		.config_init	= genphy_config_init,
#endif
		.read_status	= yt8010_read_status,
	}, {
		.phy_id		= PHY_ID_YT8510,
		.name		= "YT8510 100/10Mb Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_POLL,
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
		.config_init	= ytphy_config_init,
#else
		.config_init	= genphy_config_init,
#endif
		.read_status	= genphy_read_status,
	}, {
		.phy_id		= PHY_ID_YT8511,
		.name		= "YT8511 Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_POLL,
		.config_aneg	= genphy_config_aneg,
#if GMAC_CLOCK_INPUT_NEEDED
		.config_init	= yt8511_config_init,
#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(5, 3, 0))
		.config_init	= ytphy_config_init,
#else
		.config_init	= genphy_config_init,
#endif
#endif
		.read_status	= genphy_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8512,
		.name		= "YT8512 Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_POLL,
		.config_aneg	= genphy_config_aneg,
		.config_init	= yt8512_config_init,
		.read_status	= yt8512_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8512B,
		.name		= "YT8512B Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_POLL,
		.config_aneg	= genphy_config_aneg,
		.config_init	= yt8512_config_init,
		.read_status	= yt8512_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8521,
		.name		= "YT8521 Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset	= yt8521_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79)) 
		.aneg_done	= yt8521_aneg_done,
#endif
		.config_init	= yt8521_config_init,
		.read_status	= yt8521_read_status,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, {
		/* same as 8521 */
		.phy_id		= PHY_ID_YT8531S,
		.name		= "YT8531S Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset	= yt8521_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79)) 
		.aneg_done	= yt8521_aneg_done,
#endif
		.config_init	= yt8531S_config_init,
		.read_status	= yt8521_read_status,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, {
		/* same as 8511 */
		.phy_id		= PHY_ID_YT8531,
		.name		= "YT8531 Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_POLL,
		.config_aneg	= genphy_config_aneg,

		.config_init	= yt8531_config_init,
		.read_status	= genphy_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, {
		.phy_id		= PHY_ID_YT8618,
		.name		= "YT8618 Ethernet",
		.phy_id_mask	= MOTORCOMM_MPHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset	= yt8618_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79)) 
		.aneg_done	= yt8618_aneg_done,
#endif
		.config_init	= yt8618_config_init,
		.read_status	= yt8618_read_status,
		.suspend	= yt8618_suspend,
		.resume		= yt8618_resume,
    },
    {
		.phy_id 		= PHY_ID_YT8614,
		.name			= "YT8614 Ethernet",
		.phy_id_mask	= MOTORCOMM_MPHY_ID_MASK_8614,
		.features		= PHY_GBIT_FEATURES,
		.flags			= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset = yt8614_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
		.aneg_done		= yt8614_aneg_done,
#endif
		.config_init	= yt8614_config_init,
		.read_status	= yt8614_read_status,
		.suspend		= yt8614_suspend,
		.resume 		= yt8614_resume,
	},
	{
		.phy_id 		= PHY_ID_YT8821,
		.name			= "YT8821 2.5Gb Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK_8821,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
		.features		= PHY_GBIT_FEATURES,
#endif
		.flags			= PHY_POLL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#else
		.soft_reset     = yt8821_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 79))
		.aneg_done		= yt8521_aneg_done,
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 1, 21))
        .get_features   = yt8821_get_features,
#endif
		.config_init	= yt8821_config_init,
		.read_status	= yt8821_read_status,
		.suspend		= yt8521_suspend,
		.resume 		= yt8521_resume,
	},
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
static int ytphy_drivers_register(struct phy_driver *phy_drvs, int size)
{
	int i, j;
	int ret;

	for (i = 0; i < size; i++) {
		ret = phy_driver_register(&phy_drvs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (j = 0; j < i; j++)
		phy_driver_unregister(&phy_drvs[j]);

	return ret;
}

static void ytphy_drivers_unregister(struct phy_driver *phy_drvs, int size)
{
	int i;

	for (i = 0; i < size; i++)
		phy_driver_unregister(&phy_drvs[i]);
}

static int __init ytphy_init(void)
{
	return ytphy_drivers_register(ytphy_drvs, ARRAY_SIZE(ytphy_drvs));
}

static void __exit ytphy_exit(void)
{
	ytphy_drivers_unregister(ytphy_drvs, ARRAY_SIZE(ytphy_drvs));
}

module_init(ytphy_init);
module_exit(ytphy_exit);
#else
/* for linux 4.x */
module_phy_driver(ytphy_drvs);
#endif

MODULE_DESCRIPTION("Motorcomm PHY driver");
MODULE_AUTHOR("Leilei Zhao");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_YT8010, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8510, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8511, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8512, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8512B, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8521, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8531S, MOTORCOMM_PHY_ID_8531_MASK },
	{ PHY_ID_YT8531, MOTORCOMM_PHY_ID_8531_MASK },
	{ PHY_ID_YT8618, MOTORCOMM_MPHY_ID_MASK },
	{ PHY_ID_YT8614, MOTORCOMM_MPHY_ID_MASK_8614 },
	{ PHY_ID_YT8821, MOTORCOMM_PHY_ID_MASK_8821 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);

