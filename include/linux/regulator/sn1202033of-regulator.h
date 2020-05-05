/*
 * sn1202033of-regulator.h -- TI sn1202033
 *
 * Interface for regulator driver for TI SN1202033 Processor core supply
 *
 * Copyright (C) 2013 Samsung Electronics
 *
 * Author: Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __LINUX_REGULATOR_SN1202033_H
#define __LINUX_REGULATOR_SN1202033_H

#if defined(CONFIG_SN1202033_DEBUGGING)
#define SN1202033_DEBUG 1
#else
#define SN1202033_DEBUG 0
#endif

#define sn1202033_dbg(fmt, args...) \
	do { if (SN1202033_DEBUG) printk(fmt , ## args); } while (0)


struct sn1202033_platform_data {
	struct regulator_init_data *init_data;
	int max_volt_uV;
	int def_volt_uV;
	unsigned int vout_port;
};

#endif /* __LINUX_REGULATOR_SN1202033_H */