/*
 * tps56x20-regulator.h -- TI tps56x20
 *
 * Interface for regulator driver for TI TPS56x20 Processor core supply
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

#ifndef __LINUX_REGULATOR_TPS56X20_H
#define __LINUX_REGULATOR_TPS56X20_H

#if defined(CONFIG_TPS56x20_DEBUGGING)
#define TPS56x20_DEBUG 1
#else
#define TPS56x20_DEBUG 0
#endif

#define tps56x20_dbg(fmt, args...) \
	do { if (TPS56x20_DEBUG) printk(fmt , ## args); } while (0)


struct tps56x20_platform_data {
	struct regulator_init_data *init_data;
	int max_volt_uV;
	int def_volt_uV;
};


#endif /* __LINUX_REGULATOR_TPS56X20_H */