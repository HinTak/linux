/*
 * AHCI SATA SDP driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 * Copyright 2013  Samsung electronics
 *   Seihee Chon <sh.chon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#ifndef _AHCI_SDP_H
#define _AHCI_SDP_H

#include <linux/compiler.h>

struct device;
struct ata_port_info;

struct ahci_sdp_data {
	int (*init)(struct device *dev, void __iomem *addr);
	void (*exit)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	const struct ata_port_info *ata_port_info;
	unsigned int force_port_map;
	unsigned int mask_port_map;
	unsigned int phy_base;
	unsigned int gpr_base;
	unsigned int phy_bit;
	unsigned int link_bit;
};

#endif /* _AHCI_SDP_H */
