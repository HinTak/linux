/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_SDP_COMMON_H
#define __ARCH_SDP_COMMON_H

void sdp_map_io(void);
void sdp_init_irq(void);
void sdp_restart(char mode, const char *cmd);

extern struct smp_operations sdp_smp_ops;
extern struct sys_timer sdp_timer;

#endif /* __ARCH_SDP_COMMON_H */
