/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/reboot.h>
#include <linux/memblock.h>
#include <linux/memblock.h>
#include <asm/setup.h>
#include <asm/memory.h>

#include <asm/arch_timer.h>
#include <soc/sdp/soc.h>
#include "common.h"

/* SW chipid */
static unsigned int sdp_chipid_dt = NON_CHIPID;

static void __init sdp_check_chipid_from_of(void)
{
	if (of_machine_is_compatible("samsung,sdp1412"))
		sdp_chipid_dt = SDP1412_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1501"))
		sdp_chipid_dt = SDP1501_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1531"))
		sdp_chipid_dt = SDP1531_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1600"))
		sdp_chipid_dt = SDP1601_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1601"))
		sdp_chipid_dt = SDP1601_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1701"))
		sdp_chipid_dt = SDP1701_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1803"))
		sdp_chipid_dt = SDP1803_CHIPID;
	else if (of_machine_is_compatible("samsung,sdp1804"))
		sdp_chipid_dt = SDP1804_CHIPID;
}

/* chip id */
#define EFUSE_VALUE_SIZE	4

#define MAX_NAME_LEN	16
struct chipid_entity {
	const char	*name;
	s8		msb;
	s8		lsb;
	s8		multiplier;
	s8		unused;
};
struct sdp_chipid_info {
	int (*init)(struct sdp_chipid_info *info, struct device_node *np);
	void __iomem *regs;
	u32 values[EFUSE_VALUE_SIZE];
	struct chipid_entity entities[];
};

static struct sdp_chipid_info *sdp_chipid;

static inline unsigned char reverse_8bit(unsigned char c)
{
	c = (c & 0xf0) >> 4 | (c & 0x0f) << 4;
	c = (c & 0xcc) >> 2 | (c & 0x33) << 2;
	c = (c & 0xaa) >> 1 | (c & 0x55) << 1;
	return c;
}

static u32 get_bits(struct sdp_chipid_info *info, const s8 msb, const s8 lsb)
{
       const int bits = msb - lsb + 1;

       if ((lsb >> 5) == (msb >> 5)) {
               return (info->values[lsb >> 5] >> (lsb & 31)) & ((1 << bits) - 1);
       } else {
               const int lowbits = 32 - (lsb & 31);
               const u32 v0 = get_bits(info, lsb + lowbits - 1, lsb);
               const u32 v1 = get_bits(info, msb, lsb + lowbits);
               return v0 | (v1 << lowbits);
	}
}
static struct chipid_entity* chipid_get_entity(struct sdp_chipid_info *info, const char *prop)
{
	struct chipid_entity *e;
	for (e = &info->entities[0]; e->name; e++)
		if (!strncmp(prop, e->name, MAX_NAME_LEN))
			return e;
	return NULL;
}

static unsigned int chipid_get_value(struct sdp_chipid_info *info, struct chipid_entity *e)
{
	u32 val = get_bits(info, e->msb, e->lsb);
	if (e->multiplier)
		val *= e->multiplier;
	return val;
}

static unsigned int chipid_get_value_str(struct sdp_chipid_info *info, const char *prop)
{
	struct chipid_entity *e = chipid_get_entity(info, prop);
	if (e)
		return chipid_get_value(info, e);
	else
		return 0;
}

static int chipid_init(struct sdp_chipid_info *info, struct device_node *np)
{
	const int REG_STATUS = 0x00;
	const int REG_CTRL = 0x04;
	const int REG_VALUE[EFUSE_VALUE_SIZE] = {0x08,0x0C,0x10,0x14};
	const int REG_CRTL_SETVAL = 0x1F;
	
	int i = 0;
	struct chipid_entity *e;
	
	info->regs = of_iomap(np, 0);
	if(info->regs == NULL)
	{
		pr_err("init of_iomap failed!!\n");
		return -1;
	}

	writel(REG_CRTL_SETVAL, info->regs + REG_CTRL);
	while (readl(info->regs + REG_STATUS) != 0)
		barrier();
	
	for(i = 0 ; i < EFUSE_VALUE_SIZE ; i++)
		info->values[i] = readl(info->regs + REG_VALUE[i]);
	
	pr_warn("sdp-chipid: hw fused raw values = %08x %08x %08x %08x\n",
			info->values[3], info->values[2],
			info->values[1], info->values[0]);
       	
	for (e = &info->entities[0]; e->name; e++)
		pr_info("sdp-chipid: %s = 0x%x\n", e->name, chipid_get_value(info, e));

	/* special chip-id for jazz corner samplings */
	if (soc_is_jazz()) {
		pr_info("sdp-chipid: LOT id  = 0x%06x\n", get_bits(info, 20, 0));
		pr_info("sdp-chipid: x_pos_h = 0x%x\n", get_bits(info, 31, 26));
		pr_info("sdp-chipid: x_pos_l = 0x%x\n", get_bits(info, 33, 32));
		pr_info("sdp-chipid: y_pos   = 0x%02x\n", get_bits(info, 41, 34));
		
		/* XXX: chip-id fix for corner samplings*/
		{
			u32 lot, wafer_y;
			lot = get_bits(info, 20,0);
			
			/* y_pos : bit-level endianness reversed */
			wafer_y = get_bits(info, 41, 34);
			wafer_y = reverse_8bit(wafer_y);
			if (lot == 0x009413 && (wafer_y >= 175UL && wafer_y <= 187UL)) {
				info->values[1] &= ~(0x7 << 13);
				info->values[1] |= (2 << 13);	/* 2 := chip-id for Jazz-ML */
				pr_info("sdp-chipid: this is corner sampling, fix chip-id to Jazz-ML!\n");
			}
		}
	}

	if (soc_is_sdp1701()) {
		pr_err("sdp-chipid: kant-m2/m3, rev=%d, tmcb=%d\n", \
			sdp_get_revision_id(), chipid_get_value_str(sdp_chipid, "cpu_tmcb"));
	}

	//Hawk-P EVT0 bug fix
	if(soc_is_sdp1404() && (get_bits(info, 127, 120) || get_bits(info, 47, 44))) {
		pr_info("HawkP EVT0 ChipID Fix!\n");
		for(i = 0 ; i < EFUSE_VALUE_SIZE ; i++)
			info->values[i] = 0;
	}

	if(soc_is_sdp1412()) {		
		if(!(readl((unsigned int*)0xfe040024)&0x4)) {
			pr_info("HawkA EVT0 ChipID Fix!\n");
			for(i = 0 ; i < EFUSE_VALUE_SIZE ; i++)
				info->values[i] = 0;
		}
	}

	return (info->values[1] == 0) ? -1 : 0;
}

static void chipid_print(struct seq_file *m, struct sdp_chipid_info *info)
{
	int idx, movecnt;

	for( idx = 0; idx < EFUSE_VALUE_SIZE; idx++ ) {
		seq_printf(m, "0x%08x ", sdp_chipid->values[idx]);
	}
	seq_printf(m, "\n");
}

static struct sdp_chipid_info info_hawkp = {
	.init		= chipid_init,
	.entities	= {
		{ "revid", 44, 43, },
		{ "cpu_ids", 56, 48, 2, },
		{ "chip_ids", 74, 66, 2, },
		{ "cpu_tmcb", 63, 58, 1, },
		{},
	},
};

static struct sdp_chipid_info info_jazz= {
	.init		= chipid_init,
	.entities	=
	{
		{"revid", 44, 42,},
		{"cpu_ids", 56, 48, 2},
		{"chip_ids", 74, 66, 2},
		{"cpu_tmcb", 111, 106, 1},
		{},
	},
};
static struct sdp_chipid_info info_kantm = {
	.init		= chipid_init,
	.entities	=
	{
		{"lotid", 20, 0, },		
		{"revid", 44, 42, },
		{"cpu_ids", 56, 48, 1},
		{"core_ids", 65, 57, 1},
		{"chip_ids", 74, 66, 1},
		{"cpu_tmcb", 80, 75, 1},	/* actually this [80:75] is 'total' promise nr */
		{"sb_ids", 113, 105, 1},
		{"ft_ids", 122, 114, 1},
		{},
	},
};

static const struct of_device_id sdp_chipid_of_match[] __initconst = {
	{ .compatible	= "samsung,sdp-chipid-hawkp",
		.data = (void*)&info_hawkp },
	{ .compatible	= "samsung,sdp-chipid-jazz",
		.data = (void*)&info_jazz },
	{ .compatible	= "samsung,sdp-chipid-jazzm",
		.data = (void*)&info_jazz },
	{ .compatible	= "samsung,sdp-chipid-kantm",
		.data = (void*)&info_kantm },
	{ .compatible	= "samsung,sdp-chipid-muse",
		.data = (void*)&info_kantm },
	{},
};

static int __init sdp_chipid_of_init(void)
{
	struct device_node *np;

	/* get chipid from DT */
	sdp_check_chipid_from_of();

	/* get revision from HW chipid register */
	np = of_find_matching_node(NULL, sdp_chipid_of_match);
	if (np) {
		sdp_chipid = (struct sdp_chipid_info*)of_match_node(sdp_chipid_of_match, np)->data;
		if (sdp_chipid->init(sdp_chipid, np) < 0)
			pr_warn("SDP: cannot get revision value from HW.\n");
	}
	of_node_put(np);

	return 0;
}
core_initcall(sdp_chipid_of_init);

/* board type command line */
static enum sdp_board sdp_board_type = 0;

static int __init set_sdp_board_type_main(char *p)
{
	sdp_board_type = SDP_BOARD_MAIN;
	return 0;
}
static int __init set_sdp_board_type_jackpack(char *p)
{
	sdp_board_type = SDP_BOARD_JACKPACK;
	return 0;
}
static int __init set_sdp_board_type_lfd(char *p)
{
	sdp_board_type = SDP_BOARD_LFD;
	return 0;
}
static int __init set_sdp_board_type_sbb(char *p)
{
	sdp_board_type = SDP_BOARD_SBB;
	return 0;
}
static int __init set_sdp_board_type_hcn(char *p)
{
	sdp_board_type = SDP_BOARD_HCN;
	return 0;
}
static int __init set_sdp_board_type_vgw(char *p)
{
	sdp_board_type = SDP_BOARD_VGW;
	return 0;
}
static int __init set_sdp_board_type_fpga(char *p)
{
	sdp_board_type = SDP_BOARD_FPGA;
	return 0;
}
static int __init set_sdp_board_type_av(char *p)
{
	sdp_board_type = SDP_BOARD_AV;
	return 0;
}
static int __init set_sdp_board_type_mtv(char *p)
{
	sdp_board_type = SDP_BOARD_MTV;
	return 0;
}
static int __init set_sdp_board_type_ocm(char *p)
{
	sdp_board_type = SDP_BOARD_OCM;
	return 0;
}
static int __init set_sdp_board_type_atsc30(char *p)
{
	sdp_board_type = SDP_BOARD_ATSC30;
	return 0;
}
static int __init set_sdp_board_type_htv(char *p)
{
	sdp_board_type = SDP_BOARD_HTV;
	return 0;
}
static int __init set_sdp_board_type_ebd(char *p)
{
	sdp_board_type = SDP_BOARD_EBD;
	return 0;
}
static int __init set_sdp_board_type_oc(char *p)
{
	sdp_board_type = SDP_BOARD_OC;
	return 0;
}
static int __init set_sdp_board_type_wall(char *p)
{
	sdp_board_type = SDP_BOARD_WALL;
	return 0;
}




early_param("main", set_sdp_board_type_main);
early_param("jackpack", set_sdp_board_type_jackpack);
early_param("lfd", set_sdp_board_type_lfd);
early_param("sbb", set_sdp_board_type_sbb);
early_param("hcn", set_sdp_board_type_hcn);
early_param("vgw", set_sdp_board_type_vgw);
early_param("fpga", set_sdp_board_type_fpga);
early_param("av", set_sdp_board_type_av);
early_param("mtv", set_sdp_board_type_mtv);
early_param("ocm", set_sdp_board_type_ocm);
early_param("atsc30", set_sdp_board_type_atsc30);
early_param("htv", set_sdp_board_type_htv);
early_param("ebd", set_sdp_board_type_ebd);
early_param("oc", set_sdp_board_type_oc);
early_param("wall", set_sdp_board_type_wall);





enum sdp_board get_sdp_board_type(void)
{
	return sdp_board_type;
}
EXPORT_SYMBOL(get_sdp_board_type);

unsigned int __deprecated sdp_get_mem_cfg(int nType)
{
	BUG_ON(nType != 0 && nType != 1);
	return memblock.memory.regions[0].size;
}
EXPORT_SYMBOL(sdp_get_mem_cfg);

unsigned int __deprecated sdp_rev(void)
{
	return sdp_chipid->values[1];
}
EXPORT_SYMBOL(sdp_rev);

unsigned int __deprecated sdp_soc(void)
{
	return sdp_chipid_dt;
}
EXPORT_SYMBOL(sdp_soc);

int sdp_get_revision_id(void)
{
	return chipid_get_value_str(sdp_chipid, "revid");
}
EXPORT_SYMBOL(sdp_get_revision_id);

/* sdp proc entries - read only, persistent */
struct sdp_proc_entry {
	char name[24];
	int (*proc_read)(struct seq_file *m, void *v);
	void *private_data;
};

static int sdp_proc_show_sdpver(struct seq_file *m, void *v)
{
	seq_printf(m, "ES%d\n", sdp_get_revision_id());
	return 0;
}

static int sdp_proc_show_kmeminfo(struct seq_file *m, void *v)
{
	struct memblock_region *rg = memblock.memory.regions;
	int i;

	for(i =  0; i < memblock.memory.cnt; i++)
		seq_printf(m, "%x %x ", (u32)rg[i].base, (u32)rg[i].size);
	
	seq_printf(m, "\n");

	return 0;
}

static int sdp_proc_show_chipid(struct seq_file *m, void *v)
{
	struct sdp_proc_entry *e = m->private;
	const char *prop = e->private_data;
	struct chipid_entity *entity = chipid_get_entity(sdp_chipid, prop);
	if (entity) {
		u32 val = chipid_get_value(sdp_chipid, entity);
		seq_printf(m, "%u\n", val);
		return 0;
	} else {
		return -ENOENT;
	}
}

static int sdp_proc_dump_raw(struct seq_file *m, void *v)
{
	chipid_print(m, sdp_chipid);
	return 0;
}

static const char chip_name[MAX_SDP_CHIPID][10] = {
	"None",
	"sdp1404",
	"sdp1406f",
	"sdp1406u",
	"sdp1106",
	"sdp1412",
	"sdp1501",
	"sdp1511",
	"sdp1521",
	"sdp1531",
	"sdp1601",
	"sdp1701",
	"sdp1803",
	"sdp1804",	
};

static int sdp_proc_efuse_all(struct seq_file *m, void *v)
{
	if (sdp_chipid) {
		struct chipid_entity *e;
		for (e = &sdp_chipid->entities[0]; e->name; e++) {
			seq_printf(m, "%s : 0x%x\n", e->name, chipid_get_value(sdp_chipid, e));
		}
	}
	return 0;
}

static int sdp_proc_chip_name(struct seq_file *m, void *v)
{
	int id = sdp_soc();
	
	if(id >= MAX_SDP_CHIPID)
		id = 0;

	if(soc_is_sdp1701() && (((sdp_chipid->values[1] & (0x7 << 13)) >> 13) == 0x2))
		seq_printf(m, "sdp1711\n");
	else
		seq_printf(m, "%s\n", chip_name[id]);
	return 0;
}


static struct sdp_proc_entry sdp_proc_entries[] = {
	{
		.name		= "sdp_version",
		.proc_read	= sdp_proc_show_sdpver,
	},
	{
		.name		= "sdp_kmeminfo",
		.proc_read	= sdp_proc_show_kmeminfo,
	},
	{
		.name		= "sdp_lot_id",
		.proc_read	= sdp_proc_show_chipid,
		.private_data	= "lotid",
	},
	{
		.name		= "sdp_cpu_ids_ma",
		.proc_read	= sdp_proc_show_chipid,
		.private_data	= "cpu_ids",
	},
	{
		.name		= "sdp_chip_ids_ma",
		.proc_read	= sdp_proc_show_chipid,
		.private_data	= "chip_ids",
	},
	{
		.name		= "sdp_core_ids_ma",
		.proc_read	= sdp_proc_show_chipid,
		.private_data	= "core_ids",
	},
	{
		.name		= "sdp_cpu_tmcb",
		.proc_read	= sdp_proc_show_chipid,
		.private_data	= "cpu_tmcb",
	},
	{
		.name		= "sdp_chipid_val",
		.proc_read	= sdp_proc_dump_raw,
	},
	{
		.name		= "sdp_efuse_all",
		.proc_read	= sdp_proc_efuse_all,
	},
	{
		.name		= "chip_name",
		.proc_read	= sdp_proc_chip_name,
	},
};

static int sdp_proc_open(struct inode *inode, struct file *file)
{
	struct sdp_proc_entry *sdp_entry = PDE_DATA(inode);
	return single_open(file, sdp_entry->proc_read, sdp_entry);
}

static const struct file_operations sdp_proc_file_ops = {
	.open = sdp_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init sdp_procfs_init(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(sdp_proc_entries); i++) {
		struct sdp_proc_entry *e = &sdp_proc_entries[i];
		struct proc_dir_entry *entry = proc_create_data(
				e->name, 0644, NULL, &sdp_proc_file_ops, e);
		if (entry)
			pr_info ("/proc/%s registered.\n", e->name);
		else
			pr_err ("/proc/%s registration failed.\n", e->name);
	}
	return 0;
}
late_initcall(sdp_procfs_init);

