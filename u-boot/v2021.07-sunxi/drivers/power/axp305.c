// SPDX-License-Identifier: GPL-2.0+
/*
 * AXP305 driver
 *
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * Based on axp221.c
 * (C) Copyright 2014 Hans de Goede <hdegoede@redhat.com>
 * (C) Copyright 2013 Oliver Schinagl <oliver@schinagl.nl>
 */

#include <common.h>
#include <command.h>
#include <errno.h>
#include <asm/arch/pmic_bus.h>
#include <axp_pmic.h>

#define axp_err(fmt...) printf("[axp][err]: " fmt)
#define axp_info(fmt...) printf("[axp][info]: " fmt)

#define AXP305_DCDC4_1600MV_OFFSET 46

static u8 axp305_mvolt_to_cfg(int mvolt, int min, int max, int div)
{
	if (mvolt < min)
		mvolt = min;
	else if (mvolt > max)
		mvolt = max;

	return  (mvolt - min) / div;
}

int axp_set_dcdc4(unsigned int mvolt)
{
	int ret;
	u8 cfg;

	if (mvolt >= 1600)
		cfg = AXP305_DCDC4_1600MV_OFFSET +
			axp305_mvolt_to_cfg(mvolt, 1600, 3300, 100);
	else
		cfg = axp305_mvolt_to_cfg(mvolt, 600, 1500, 20);

	if (mvolt == 0)
		return pmic_bus_clrbits(AXP305_OUTPUT_CTRL1,
					AXP305_OUTPUT_CTRL1_DCDCD_EN);

	ret = pmic_bus_write(AXP305_DCDCD_VOLTAGE, cfg);
	if (ret)
		return ret;

	return pmic_bus_setbits(AXP305_OUTPUT_CTRL1,
				AXP305_OUTPUT_CTRL1_DCDCD_EN);
}

int axp_init(void)
{
	u8 axp_chip_id;
	int ret;

	printf("axp305 init!\n");

	ret = pmic_bus_init();
	if (ret)
		return ret;

	// ret = pmic_bus_read(AXP305_CHIP_VERSION, &axp_chip_id);
	ret = pmic_bus_read(AXP1530_VERSION, &axp_chip_id);
	if (ret)
	{
		printf("axp read error!\n");
		return ret;
	}
	else
	{
		printf("chip_id: %d  \n",axp_chip_id);
	}

	axp_chip_id &= 0XCF;
	if (axp_chip_id == AXP1530_CHIP_ID || axp_chip_id == AXP313A_CHIP_ID || axp_chip_id == AXP313B_CHIP_ID) {
		/*pmu type AXP1530*/
		// pmu_axp1530_necessary_reg_enable();
		printf("PMU: AXP1530\n");
		return 0;
	}
		
	// if ((axp_chip_id & AXP305_CHIP_VERSION_MASK) != 0x40)
	// 	return -ENODEV;

	return ret;
}

#ifndef CONFIG_PSCI_RESET
int do_poweroff(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	pmic_bus_write(AXP305_SHUTDOWN, AXP305_POWEROFF);

	/* infinite loop during shutdown */
	while (1) {}

	/* not reached */
	return 0;
}
#endif


//------------------------------------------------------------------------------
// axp1530

typedef struct _axp_contrl_info {
	char name[16];

	u32 min_vol;
	u32 max_vol;
	u32 cfg_reg_addr;
	u32 cfg_reg_mask;

	u32 step0_val;
	u32 split1_val;
	u32 step1_val;
	u32 ctrl_reg_addr;

	u32 ctrl_bit_ofs;
	u32 step2_val;
	u32 split2_val;
} axp_contrl_info;

__attribute__((section(".data"))) axp_contrl_info pmu_axp1530_ctrl_tbl[] = {
	/*name,    min,  max, reg,  mask, step0,split1_val, step1,ctrl_reg,ctrl_bit */
	{ "dcdc1", 500, 3400, AXP1530_DC1OUT_VOL, 0x7f, 10, 1200, 20,
	  AXP1530_OUTPUT_POWER_ON_OFF_CTL, 0, 100, 1540 },
	{ "dcdc2", 500, 1540, AXP1530_DC2OUT_VOL, 0x7f, 10, 1200, 20,
	  AXP1530_OUTPUT_POWER_ON_OFF_CTL, 1 },
	{ "dcdc3", 500, 1840, AXP1530_DC3OUT_VOL, 0x7f, 10, 1200, 20,
	  AXP1530_OUTPUT_POWER_ON_OFF_CTL, 2 },
	{ "aldo1", 500, 3500, AXP1530_ALDO1OUT_VOL, 0x1f, 100, 0, 0,
	  AXP1530_OUTPUT_POWER_ON_OFF_CTL, 3 },
	{ "dldo1", 500, 3500, AXP1530_DLDO1OUT_VOL, 0x1f, 100, 0, 0,
	  AXP1530_OUTPUT_POWER_ON_OFF_CTL, 4 },
};

static axp_contrl_info *get_ctrl_info_from_tbl(char *name)
{
	int i    = 0;
	int size = ARRAY_SIZE(pmu_axp1530_ctrl_tbl);
	axp_contrl_info *p;

	for (i = 0; i < size; i++) {
		if (!strncmp(name, pmu_axp1530_ctrl_tbl[i].name,
			     strlen(pmu_axp1530_ctrl_tbl[i].name))) {
			break;
		}
	}
	if (i >= size) {
		axp_err("can't find %s from table\n", name);
		return NULL;
	}
	p = pmu_axp1530_ctrl_tbl + i;
	return p;
}

int pmu_axp1530_set_voltage(char *name, unsigned int set_vol, unsigned int onoff)
{
	u8 reg_value;
	axp_contrl_info *p_item = NULL;
	u8 base_step		= 0;

	p_item = get_ctrl_info_from_tbl(name);
	if (!p_item) {
		return -1;
	}

	axp_info(
		"name %s, min_vol %dmv, max_vol %d, cfg_reg 0x%x, cfg_mask 0x%x \
		step0_val %d, split1_val %d, step1_val %d, ctrl_reg_addr 0x%x, ctrl_bit_ofs %d\n",
		p_item->name, p_item->min_vol, p_item->max_vol,
		p_item->cfg_reg_addr, p_item->cfg_reg_mask, p_item->step0_val,
		p_item->split1_val, p_item->step1_val, p_item->ctrl_reg_addr,
		p_item->ctrl_bit_ofs);

	if ((set_vol > 0) && (p_item->min_vol)) {
		if (set_vol < p_item->min_vol) {
			set_vol = p_item->min_vol;
		} else if (set_vol > p_item->max_vol) {
			set_vol = p_item->max_vol;
		}
		if (pmic_bus_read(p_item->cfg_reg_addr,
				  &reg_value)) {
			return -1;
		}

		reg_value &= ~p_item->cfg_reg_mask;
		if (p_item->split2_val && (set_vol > p_item->split2_val)) {
			base_step = (p_item->split2_val - p_item->split1_val) /
				    p_item->step1_val;

			base_step += (p_item->split1_val - p_item->min_vol) /
				     p_item->step0_val;
			reg_value |= (base_step +
				      (set_vol - p_item->split2_val/p_item->step2_val*p_item->step2_val) /
					      p_item->step2_val);
		} else if (p_item->split1_val &&
			   (set_vol > p_item->split1_val)) {
			if (p_item->split1_val < p_item->min_vol) {
				axp_err("bad split val(%d) for %s\n",
					p_item->split1_val, name);
			}

			base_step = (p_item->split1_val - p_item->min_vol) /
				    p_item->step0_val;
			reg_value |= (base_step +
				      (set_vol - p_item->split1_val) /
					      p_item->step1_val);
		} else {
			reg_value |=
				(set_vol - p_item->min_vol) / p_item->step0_val;
		}
		if (pmic_bus_write(p_item->cfg_reg_addr, reg_value)) {
			axp_err("unable to set %s\n", name);
			return -1;
		}
	}

	if (onoff < 0) {
		return 0;
	}
	if (pmic_bus_read(p_item->ctrl_reg_addr,
			  &reg_value)) {
		return -1;
	}
	if (onoff == 0) {
		reg_value &= ~(1 << p_item->ctrl_bit_ofs);
	} else {
		reg_value |= (1 << p_item->ctrl_bit_ofs);
	}
	if (pmic_bus_write(p_item->ctrl_reg_addr, reg_value)) {
		axp_err("unable to onoff %s\n", name);
		return -1;
	}
	return 0;
}

int pmu_axp1530_get_voltage(char *name)
{
	u8 reg_value;
	axp_contrl_info *p_item = NULL;
	u8 base_step;
	int vol;

	p_item = get_ctrl_info_from_tbl(name);
	if (!p_item) {
		return -1;
	}

	if (pmic_bus_read( p_item->ctrl_reg_addr, &reg_value)) {
		return -1;
	}
	if (!(reg_value & (0x01 << p_item->ctrl_bit_ofs))) {
		return 0;
	}

	if (pmic_bus_read( p_item->cfg_reg_addr, &reg_value)) {
		return -1;
	}
	reg_value &= p_item->cfg_reg_mask;
	if (p_item->split2_val) {
		u32 base_step2;
		base_step = (p_item->split1_val - p_item->min_vol) /
				     p_item->step0_val;

		base_step2 = base_step + (p_item->split2_val - p_item->split1_val) /
			    p_item->step1_val;

		if (reg_value >= base_step2) {
			vol = ALIGN(p_item->split2_val, p_item->step2_val) +
			      p_item->step2_val * (reg_value - base_step2);
		} else if (reg_value >= base_step) {
			vol = p_item->split1_val +
			      p_item->step1_val * (reg_value - base_step);
		} else {
			vol = p_item->min_vol + p_item->step0_val * reg_value;
		}
	} else if (p_item->split1_val) {
		base_step = (p_item->split1_val - p_item->min_vol) /
			    p_item->step0_val;
		if (reg_value > base_step) {
			vol = p_item->split1_val +
			      p_item->step1_val * (reg_value - base_step);
		} else {
			vol = p_item->min_vol + p_item->step0_val * reg_value;
		}
	} else {
		vol = p_item->min_vol + p_item->step0_val * reg_value;
	}
	return vol;
}


