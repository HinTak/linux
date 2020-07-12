/*
 * Copyright 2017-2018 NXP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/busfreq-imx.h>
#include <linux/module.h>

struct busfreq_api busfreq_callback = {
	.request = NULL,
	.release = NULL,
	.get_mode = NULL,
};

void request_bus_freq(enum bus_freq_mode mode)
{
	if (busfreq_callback.request)
		busfreq_callback.request(mode);
}
EXPORT_SYMBOL(request_bus_freq);

void release_bus_freq(enum bus_freq_mode mode)
{
	if (busfreq_callback.release)
		busfreq_callback.release(mode);
}
EXPORT_SYMBOL(release_bus_freq);

int get_bus_freq_mode(void)
{
	if (busfreq_callback.get_mode)
		return busfreq_callback.get_mode();
	return -EINVAL;
}
EXPORT_SYMBOL(get_bus_freq_mode);
