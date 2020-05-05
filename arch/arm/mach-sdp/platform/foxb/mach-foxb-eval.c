/* mach-foxb-eval.c
 *
 * foxb evaluation board specific initialization.
 *
 * Copyright (C) 2013 Samsung Electronics, All Rights Reserved.
 *
 * Original code: Sola lee <sssol.lee@samsung.com>
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regulator/sn1202033.h>

static struct regulator_consumer_supply sn1202033_supply[] = {
	{
		.supply = "VDD_CORE",
		.dev_name = NULL,
	},
	{
		.supply = "VDD_CPU",
		.dev_name = NULL,
	},
};

static struct regulator_init_data sn1202033_data[] = {
	[0] = {
		.constraints	= {
			.name		= "VDD_CORE range",
			.min_uV		= 680000,
			.max_uV		= 1950000,
			.always_on	= 1,
			.boot_on	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = 1,
		.consumer_supplies = &sn1202033_supply[0],
	},
	[1] = {
		.constraints	= {
			.name		= "VDD_CPU range",
			.min_uV		= 680000,
			.max_uV		= 1950000,
			.always_on	= 1,
			.boot_on	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = 1,
		.consumer_supplies = &sn1202033_supply[1],
	},
};

static struct sn1202033_platform_data sn1202033_regulator[] = {
	[0] = {
		.i2c_addr	= 0xC0,
		.i2c_port	= 3,
		.def_volt	= 1100000,
		.vout_port	= 1,
		.init_data	= &sn1202033_data[0],
	},
	[1] = {
		.i2c_addr	= 0xC0,
		.i2c_port	= 3,
		.def_volt	= 1200000,
		.vout_port	= 2,
		.init_data	= &sn1202033_data[1],
	},	
};

static struct platform_device sdp_pmic_sn1202033[] = {
	[0] = {
		.name	= "sdp_sn1202033",
		.id		= 0,
		.dev	= {
			.platform_data = &sn1202033_regulator[0],
		},
	},
	[1] = {
		.name	= "sdp_sn1202033",
		.id		= 1,
		.dev	= {
			.platform_data = &sn1202033_regulator[1],
		},
	},
};

static struct platform_device* foxb_init_devs[] __initdata = {
	&sdp_pmic_sn1202033[0],
	&sdp_pmic_sn1202033[1],
};

void __init foxb_board_init(void)
{
	platform_add_devices(foxb_init_devs, ARRAY_SIZE(foxb_init_devs));
}

