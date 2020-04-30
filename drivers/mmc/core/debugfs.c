/*
 * Debugfs support for hosts and cards
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fault-inject.h>
#include <linux/uaccess.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "core.h"
#include "mmc_ops.h"

#ifdef CONFIG_FAIL_MMC_REQUEST

static DECLARE_FAULT_ATTR(fail_default_attr);
static char *fail_request;
module_param(fail_request, charp, 0);

#endif /* CONFIG_FAIL_MMC_REQUEST */

#ifndef CONFIG_VD_RELEASE
static int mmc_cmd_tool(struct mmc_host *host, int cmd)
{
	const char *str;
	u32 rocr, ocr;
	u32 cid[4];
	struct mmc_card *card = host->card;

	mmc_claim_host(host);

	if(cmd == 0)
	{
		pr_warning("[Start MMC Reset flow]\n");
		pr_warning("[MMC Power Off]\n");
		mmc_power_off(host);
		pr_warning("... Done ... \n");

		pr_warning("[MMC Power ON]\n");
		mmc_power_up(host, host->ocr_avail);
		pr_warning("... Done ... \n\n");

		pr_warning("[MMC H/W Reset]\n");
		mmc_host_clk_hold(host);
		host->ops->hw_reset(host);
		mmc_host_clk_release(host);
		pr_warning("... Done ... \n\n");
	}
	else if(cmd == 1)
	{
		pr_warning("[Start MMC Init flow]\n");
		pr_warning("[MMC GO IDLE - CMD0]\n");
		mmc_go_idle(host);
		pr_warning("... Done ... \n\n");

		pr_warning("[MMC send op cond - CMD1]\n");
		mmc_send_op_cond(host, 0x40ff8080 | (1 << 30), &rocr);
		pr_warning("... Done ... \n\n");

		pr_warning("[MMC send cid - CMD2]\n");
		mmc_all_send_cid(host, cid);
		pr_warning("... Done ... \n\n");

		pr_warning("[MMC set relative addr - CMD3]\n");
		mmc_set_relative_addr(card);
		mmc_set_bus_mode(host, MMC_BUSMODE_PUSHPULL);
		pr_warning("... Done ... \n\n");

		pr_warning("[MMC select card - CMD7]\n");
		mmc_select_card(card);
		pr_warning("... Done ... \n\n");

		/* NOTE : below init should be invoked for normal eMMC work */
		/* mmc_select_hs400es(card); */
	}
	else if(cmd == 2)
	{
		unsigned int timeout = card->ext_csd.generic_cmd6_time;
		int err;

		pr_warning("[MMC PoN SHORT - CMD6 / 34 / 2]\n");
		err = mmc_flush_cache(card);
		if (err)
			goto out;

		err = __mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
				   EXT_CSD_POWER_OFF_NOTIFICATION,
				   EXT_CSD_POWER_OFF_SHORT,
				   timeout, true, false, false);
		if (err)
			pr_err("%s: Power Off Notification timed out, %u\n",
			       mmc_hostname(card->host), timeout);

		/* Disable the power off notification after the switch operation. */
		card->ext_csd.power_off_notification = EXT_CSD_NO_POWER_NOTIFICATION;
	}
	else if(cmd == 3)
	{
		unsigned int timeout = card->ext_csd.power_off_longtime;
		int err;

		pr_warning("[MMC PoN LONG  - CMD6 / 34 / 3]\n");
		err = mmc_flush_cache(card);
		if (err)
			goto out;

		err = __mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
				   EXT_CSD_POWER_OFF_NOTIFICATION,
				   EXT_CSD_POWER_OFF_LONG,
				   timeout, true, false, false);
		if (err)
			pr_err("%s: Power Off Notification timed out, %u\n",
			       mmc_hostname(card->host), timeout);

		/* Disable the power off notification after the switch operation. */
		card->ext_csd.power_off_notification = EXT_CSD_NO_POWER_NOTIFICATION;
	}
	mmc_release_host(host);

	return 0;
out :
	pr_warning("err??- cmd : %d\n",cmd);
	return 0;
}

ssize_t mmc_cmd_write(struct file *file, const char __user *buf,
		      size_t count, loff_t *ppos)
{
	struct seq_file *seq  = file->private_data;
	struct mmc_host *host = seq->private;

	char kbuf[8], *pos;
	bool setgroups_allowed;
	ssize_t ret;

	/* Only allow a very narrow range of strings to be written */
	ret = -EINVAL;
	if ((*ppos != 0) || (count >= sizeof(kbuf)))
		goto out;

	/* What was written? */
	ret = -EFAULT;
	if (copy_from_user(kbuf, buf, count))
		goto out;
	kbuf[count] = '\0';
	pos = kbuf;

	/* What is being requested? */
	ret = -EINVAL;

	if (strncmp(pos, "0", 1) == 0)
	{
		mmc_cmd_tool(host, 0);
	}
	else if (strncmp(pos, "1", 1) == 0)
	{
		mmc_cmd_tool(host, 1);
	}
	else if (strncmp(pos, "2", 1) == 0)
	{
		mmc_cmd_tool(host, 2);
	}
	else if (strncmp(pos, "3", 1) == 0)
	{
		mmc_cmd_tool(host, 3);
	}
	else
	{
		pr_warning("check usage\n");
	}


	/* Report a successful write */
	*ppos = count;
	ret = count;
out:
	return ret;
}

static int mmc_cmd_show(struct seq_file *s, void *data)
{
	pr_warning("Please Check the Usage. \n");
	pr_warning("Usage :\n");
	pr_warning("- eMMC Reset\n");
	pr_warning("#>echo 0 > /sys/kernel/debug/mmc0/mmc_cmd_test\n\n");
	pr_warning("- eMMC Init\n");
	pr_warning("#>echo 1 > /sys/kernel/debug/mmc0/mmc_cmd_test\n\n");
	pr_warning("- eMMC PoN SHORT - CMD6 / 34 / 2\n");
	pr_warning("#>echo 2 > /sys/kernel/debug/mmc0/mmc_cmd_test\n\n");
	pr_warning("- eMMC PoN LONG  - CMD6 / 34 / 3\n");
	pr_warning("#>echo 3 > /sys/kernel/debug/mmc0/mmc_cmd_test\n");

	return 0;
}

static int mmc_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_cmd_show, inode->i_private);
}

static const struct file_operations mmc_cmd_fops = {
	.open		= mmc_cmd_open,
	.write		= mmc_cmd_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

/* The debugfs functions are optimized away when CONFIG_DEBUG_FS isn't set. */
static int mmc_ios_show(struct seq_file *s, void *data)
{
	static const char *vdd_str[] = {
		[8]	= "2.0",
		[9]	= "2.1",
		[10]	= "2.2",
		[11]	= "2.3",
		[12]	= "2.4",
		[13]	= "2.5",
		[14]	= "2.6",
		[15]	= "2.7",
		[16]	= "2.8",
		[17]	= "2.9",
		[18]	= "3.0",
		[19]	= "3.1",
		[20]	= "3.2",
		[21]	= "3.3",
		[22]	= "3.4",
		[23]	= "3.5",
		[24]	= "3.6",
	};
	struct mmc_host	*host = s->private;
	struct mmc_ios	*ios = &host->ios;
	const char *str;

	seq_printf(s, "clock:\t\t%u Hz\n", ios->clock);
	if (host->actual_clock)
		seq_printf(s, "actual clock:\t%u Hz\n", host->actual_clock);
	seq_printf(s, "vdd:\t\t%u ", ios->vdd);
	if ((1 << ios->vdd) & MMC_VDD_165_195)
		seq_printf(s, "(1.65 - 1.95 V)\n");
	else if (ios->vdd < (ARRAY_SIZE(vdd_str) - 1)
			&& vdd_str[ios->vdd] && vdd_str[ios->vdd + 1])
		seq_printf(s, "(%s ~ %s V)\n", vdd_str[ios->vdd],
				vdd_str[ios->vdd + 1]);
	else
		seq_printf(s, "(invalid)\n");

	switch (ios->bus_mode) {
	case MMC_BUSMODE_OPENDRAIN:
		str = "open drain";
		break;
	case MMC_BUSMODE_PUSHPULL:
		str = "push-pull";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "bus mode:\t%u (%s)\n", ios->bus_mode, str);

	switch (ios->chip_select) {
	case MMC_CS_DONTCARE:
		str = "don't care";
		break;
	case MMC_CS_HIGH:
		str = "active high";
		break;
	case MMC_CS_LOW:
		str = "active low";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "chip select:\t%u (%s)\n", ios->chip_select, str);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		str = "off";
		break;
	case MMC_POWER_UP:
		str = "up";
		break;
	case MMC_POWER_ON:
		str = "on";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "power mode:\t%u (%s)\n", ios->power_mode, str);
	seq_printf(s, "bus width:\t%u (%u bits)\n",
			ios->bus_width, 1 << ios->bus_width);

	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
		str = "legacy";
		break;
	case MMC_TIMING_MMC_HS:
		str = "mmc high-speed";
		break;
	case MMC_TIMING_SD_HS:
		str = "sd high-speed";
		break;
	case MMC_TIMING_UHS_SDR50:
		str = "sd uhs SDR50";
		break;
	case MMC_TIMING_UHS_SDR104:
		str = "sd uhs SDR104";
		break;
	case MMC_TIMING_UHS_DDR50:
		str = "sd uhs DDR50";
		break;
	case MMC_TIMING_MMC_DDR52:
		str = "mmc DDR52";
		break;
	case MMC_TIMING_MMC_HS200:
		str = "mmc HS200";
		break;
	case MMC_TIMING_MMC_HS400:
		str = mmc_card_hs400es(host->card) ?
			"mmc HS400 enhanced strobe" : "mmc HS400";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "timing spec:\t%u (%s)\n", ios->timing, str);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		str = "3.30 V";
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		str = "1.80 V";
		break;
	case MMC_SIGNAL_VOLTAGE_120:
		str = "1.20 V";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "signal voltage:\t%u (%s)\n", ios->chip_select, str);

	return 0;
}

static int mmc_ios_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_ios_show, inode->i_private);
}

static const struct file_operations mmc_ios_fops = {
	.open		= mmc_ios_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int mmc_clock_opt_get(void *data, u64 *val)
{
	struct mmc_host *host = data;

	*val = host->ios.clock;

	return 0;
}

static int mmc_clock_opt_set(void *data, u64 val)
{
	struct mmc_host *host = data;

	/* We need this check due to input value is u64 */
	if (val > host->f_max)
		return -EINVAL;

	mmc_claim_host(host);
	mmc_set_clock(host, (unsigned int) val);
	mmc_release_host(host);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmc_clock_fops, mmc_clock_opt_get, mmc_clock_opt_set,
	"%llu\n");

void mmc_add_host_debugfs(struct mmc_host *host)
{
	struct dentry *root;

	root = debugfs_create_dir(mmc_hostname(host), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	host->debugfs_root = root;

	if (!debugfs_create_file("ios", S_IRUSR, root, host, &mmc_ios_fops))
		goto err_node;

	if (!debugfs_create_file("clock", S_IRUSR | S_IWUSR, root, host,
			&mmc_clock_fops))
		goto err_node;

#ifdef CONFIG_MMC_CLKGATE
	if (!debugfs_create_u32("clk_delay", (S_IRUSR | S_IWUSR),
				root, &host->clk_delay))
		goto err_node;
#endif
#ifdef CONFIG_FAIL_MMC_REQUEST
	if (fail_request)
		setup_fault_attr(&fail_default_attr, fail_request);
	host->fail_mmc_request = fail_default_attr;
	if (IS_ERR(fault_create_debugfs_attr("fail_mmc_request",
					     root,
					     &host->fail_mmc_request)))
		goto err_node;
#endif
#ifndef CONFIG_VD_RELEASE
	if (!debugfs_create_file("mmc_cmd_test", S_IRUSR | S_IWUSR, root, host,
			&mmc_cmd_fops))
		goto err_node;
#endif
	return;

err_node:
	debugfs_remove_recursive(root);
	host->debugfs_root = NULL;
err_root:
	dev_err(&host->class_dev, "failed to initialize debugfs\n");
}

void mmc_remove_host_debugfs(struct mmc_host *host)
{
	debugfs_remove_recursive(host->debugfs_root);
}

static int mmc_dbg_card_status_get(void *data, u64 *val)
{
	struct mmc_card	*card = data;
	u32		status;
	int		ret;

	mmc_get_card(card);

	ret = mmc_send_status(data, &status);
	if (!ret)
		*val = status;

	mmc_put_card(card);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(mmc_dbg_card_status_fops, mmc_dbg_card_status_get,
		NULL, "%08llx\n");

static int mmc_ext_csd_read(struct seq_file *s, void *data)
{
	struct mmc_card *card = s->private;
	u8 *ext_csd;
	u8 ext_csd_rev;
	int err;
	const char *str;

	mmc_get_card(card);
	err = mmc_get_ext_csd(card, &ext_csd);
	mmc_put_card(card);
	if (err)
		goto out_free;

	ext_csd_rev = ext_csd[192];

	switch (ext_csd_rev) {
	case 8:
		str = "5.1";
		break;
	case 7:
		str = "5.0/5.01";
		break;
	case 6:
		str = "4.5/4.51";
		break;
	case 5:
		str = "4.41";
		break;
	case 3:
		str = "4.3";
		break;
	case 2:
		str = "4.2";
		break;
	case 1:
		str = "4.1";
		break;
	default:
		goto out_free;
	}
	seq_printf(s, "Extended CSD rev 1.%d (MMC %s)\n", ext_csd_rev, str);

	if (ext_csd_rev < 3)
		goto out_free; /* No ext_csd */

	/* Parse the Extended CSD registers.
	 * Reserved bit should be read as "0" in case of spec older
	 * than A441.
	 */
	seq_printf(s, "s_cmd_set[504]            : 0x%02x\n", ext_csd[504]);
	seq_printf(s, "hpi_features[503]         : 0x%02x\n", ext_csd[503]);
	seq_printf(s, "blops_support[502]        : 0x%02x\n", ext_csd[502]);

	if (ext_csd_rev >= 6) { /* eMMC 4.5 */
		unsigned int cache_size = ext_csd[249] << 0 |
			ext_csd[250] << 8 |
			ext_csd[251] << 16 |
			ext_csd[252] << 24;
		seq_printf(s, "max_packed_reads[501]     : 0x%02x\n", ext_csd[501]);
		seq_printf(s, "max_packed_writes[500]    : 0x%02x\n", ext_csd[500]);
		seq_printf(s, "data_tag_support[499]     : 0x%02x\n", ext_csd[499]);
		seq_printf(s, "tag_unit_size[498]        : 0x%02x\n", ext_csd[498]);
		seq_printf(s, "tag_res_size[497]         : 0x%02x\n", ext_csd[497]);
		seq_printf(s, "context_capability[496]   : 0x%02x\n", ext_csd[496]);
		seq_printf(s, "large_unit_size_m1[495]   : 0x%02x\n", ext_csd[495]);
		seq_printf(s, "ext_support[494]          : 0x%02x\n", ext_csd[494]);
		if (cache_size)
			seq_printf(s, "cache_size[]              : %d KiB\n", cache_size);
		else
			seq_puts(s, "No cache existing\n");

		seq_printf(s, "generic_cmd6_time[248]    : 0x%02x\n", ext_csd[248]);
		seq_printf(s, "power_off_long_time[247]  : 0x%02x\n", ext_csd[247]);
	}

	/* A441: Reserved [501:247]
A43: reserved [246:229] */
	if (ext_csd_rev >= 5) {
		seq_printf(s, "ini_timeout_ap[241]       : 0x%02x\n", ext_csd[241]);
		/* A441: reserved [240] */
		seq_printf(s, "pwr_cl_ddr_52_360[239]    : 0x%02x\n", ext_csd[239]);
		seq_printf(s, "pwr_cl_ddr_52_195[238]    : 0x%02x\n", ext_csd[238]);

		/* A441: reserved [237-236] */

		if (ext_csd_rev >= 6) {
			seq_printf(s, "pwr_cl_200_360[237]       : 0x%02x\n", ext_csd[237]);
			seq_printf(s, "pwr_cl_200_195[236]       : 0x%02x\n", ext_csd[236]);
		}

		seq_printf(s, "min_perf_ddr_w_8_52[235]  : 0x%02x\n", ext_csd[235]);
		seq_printf(s, "min_perf_ddr_r_8_52[234]  : 0x%02x\n", ext_csd[234]);
		/* A441: reserved [233] */
		seq_printf(s, "trim_mult[232]            : 0x%02x\n", ext_csd[232]);
		seq_printf(s, "sec_feature_support[231]  : 0x%02x\n", ext_csd[231]);
	}
	if (ext_csd_rev == 5) { /* Obsolete in 4.5 */
		seq_printf(s, "sec_erase_mult[230]       : 0x%02x\n", ext_csd[230]);
		seq_printf(s, "sec_trim_mult[229]        : 0x%02x\n", ext_csd[229]);
	}
	seq_printf(s, "boot_info[228]            : 0x%02x\n", ext_csd[228]);
	/* A441/A43: reserved [227] */
	seq_printf(s, "boot_size_multi[226]      : 0x%02x\n", ext_csd[226]);
	seq_printf(s, "acc_size[225]             : 0x%02x\n", ext_csd[225]);
	seq_printf(s, "hc_erase_grp_size[224]    : 0x%02x\n", ext_csd[224]);
	seq_printf(s, "erase_timeout_mult[223]   : 0x%02x\n", ext_csd[223]);
	seq_printf(s, "rel_wr_sec_c[222]         : 0x%02x\n", ext_csd[222]);
	seq_printf(s, "hc_wp_grp_size[221]       : 0x%02x\n", ext_csd[221]);
	seq_printf(s, "s_c_vcc[220]              : 0x%02x\n", ext_csd[220]);
	seq_printf(s, "s_c_vccq[219]             : 0x%02x\n", ext_csd[219]);
	/* A441/A43: reserved [218] */
	seq_printf(s, "s_a_timeout[217]          : 0x%02x\n", ext_csd[217]);
	/* A441/A43: reserved [216] */
	seq_printf(s, "sec_count                 : 0x%02x\n",
			(ext_csd[215] << 24) |
			(ext_csd[214] << 16) | (ext_csd[213] << 8)  |
			ext_csd[212]);
	/* A441/A43: reserved [211] */
	seq_printf(s, "min_perf_w_8_52[210]      : 0x%02x\n", ext_csd[210]);
	seq_printf(s, "min_perf_r_8_52[209]      : 0x%02x\n", ext_csd[209]);
	seq_printf(s, "min_perf_w_8_26_4_52[208] : 0x%02x\n", ext_csd[208]);
	seq_printf(s, "min_perf_r_8_26_4_52[207] : 0x%02x\n", ext_csd[207]);
	seq_printf(s, "min_perf_w_4_26[206]      : 0x%02x\n", ext_csd[206]);
	seq_printf(s, "min_perf_r_4_26[205]      : 0x%02x\n", ext_csd[205]);
	/* A441/A43: reserved [204] */
	seq_printf(s, "pwr_cl_26_360[203]        : 0x%02x\n", ext_csd[203]);
	seq_printf(s, "pwr_cl_52_360[202]        : 0x%02x\n", ext_csd[202]);
	seq_printf(s, "pwr_cl_26_195[201]        : 0x%02x\n", ext_csd[201]);
	seq_printf(s, "pwr_cl_52_195[200]        : 0x%02x\n", ext_csd[200]);

	/* A43: reserved [199:198] */
	if (ext_csd_rev >= 5) {
		seq_printf(s, "partition_switch_time[199]: 0x%02x ([FlashFS] ext_csd.part_time set as twice from this value)\n", ext_csd[199]);
		seq_printf(s, "out_of_interrupt_time[198]: 0x%02x\n", ext_csd[198]);
	}

	/* A441/A43: reserved	[197] [195] [193] [190] [188]
	 * [186] [184] [182] [180] [176] */

	if (ext_csd_rev >= 6)
		seq_printf(s, "driver_strength[197]      : 0x%02x\n", ext_csd[197]);

	seq_printf(s, "card_type[196]            : 0x%02x\n", ext_csd[196]);
	seq_printf(s, "csd_structure[194]        : 0x%02x\n", ext_csd[194]);
	seq_printf(s, "ext_csd_rev[192]          : 0x%02x\n", ext_csd[192]);
	seq_printf(s, "cmd_set[191]              : 0x%02x\n", ext_csd[191]);
	seq_printf(s, "cmd_set_rev[189]          : 0x%02x\n", ext_csd[189]);
	seq_printf(s, "power_class[187]          : 0x%02x\n", ext_csd[187]);
	seq_printf(s, "hs_timing[185]            : 0x%02x\n", ext_csd[185]);
	seq_printf(s, "bus_width[183]            : 0x%02x\n", ext_csd[183]);
	seq_printf(s, "erased_mem_cont[181]      : 0x%02x\n", ext_csd[181]);
	seq_printf(s, "partition_config[179]     : 0x%02x\n", ext_csd[179]);
	seq_printf(s, "boot_config_prot[178]     : 0x%02x\n", ext_csd[178]);
	seq_printf(s, "boot_bus_width[177]       : 0x%02x\n", ext_csd[177]);
	seq_printf(s, "erase_group_def[175]      : 0x%02x\n", ext_csd[175]);

	/* A43: reserved [174:0] / A441: reserved [174] */
	if (ext_csd_rev >= 5) {
		seq_printf(s, "boot_wp[173]              : 0x%02x\n", ext_csd[173]);
		/* A441: reserved [172] */
		seq_printf(s, "user_wp[171]              : 0x%02x\n", ext_csd[171]);
		/* A441: reserved [170] */
		seq_printf(s, "fw_config[170]            : 0x%02x\n", ext_csd[169]);
		seq_printf(s, "rpmb_size_mult[168]       : 0x%02x\n", ext_csd[168]);
		seq_printf(s, "wr_rel_set[167]           : 0x%02x\n", ext_csd[167]);
		seq_printf(s, "wr_rel_param[166]         : 0x%02x\n", ext_csd[166]);
		/* A441: reserved [165] */
		seq_printf(s, "bkops_start[164]          : 0x%02x\n", ext_csd[164]);
		seq_printf(s, "bkops_en[163]             : 0x%02x\n", ext_csd[163]);
		seq_printf(s, "rst_n_function[162]       : 0x%02x\n", ext_csd[162]);
		seq_printf(s, "hpi_mgmt[161]             : 0x%02x\n", ext_csd[161]);
		seq_printf(s, "partitioning_support[160] : 0x%02x\n", ext_csd[160]);
		seq_printf(s, "max_enh_size_mult[2]      : 0x%02x\n", ext_csd[159]);
		seq_printf(s, "max_enh_size_mult[1]      : 0x%02x\n", ext_csd[158]);
		seq_printf(s, "max_enh_size_mult[0]      : 0x%02x\n", ext_csd[157]);
		seq_printf(s, "partitions_attribute[156] : 0x%02x\n", ext_csd[156]);
		seq_printf(s, "partition_setting_completed: 0x%02x\n",
				ext_csd[155]);
		seq_printf(s, "gp_size_mult_4|2          : 0x%02x\n", ext_csd[154]);
		seq_printf(s, "gp_size_mult_4|1          : 0x%02x\n", ext_csd[153]);
		seq_printf(s, "gp_size_mult_4|0          : 0x%02x\n", ext_csd[152]);
		seq_printf(s, "gp_size_mult_3|2          : 0x%02x\n", ext_csd[151]);
		seq_printf(s, "gp_size_mult_3|1          : 0x%02x\n", ext_csd[150]);
		seq_printf(s, "gp_size_mult_3|0          : 0x%02x\n", ext_csd[149]);
		seq_printf(s, "gp_size_mult_2|2          : 0x%02x\n", ext_csd[148]);
		seq_printf(s, "gp_size_mult_2|1          : 0x%02x\n", ext_csd[147]);
		seq_printf(s, "gp_size_mult_2|0          : 0x%02x\n", ext_csd[146]);
		seq_printf(s, "gp_size_mult_1|2          : 0x%02x\n", ext_csd[145]);
		seq_printf(s, "gp_size_mult_1|1          : 0x%02x\n", ext_csd[144]);
		seq_printf(s, "gp_size_mult_1|0          : 0x%02x\n", ext_csd[143]);
		seq_printf(s, "enh_size_mult|2           : 0x%02x\n", ext_csd[142]);
		seq_printf(s, "enh_size_mult[1           : 0x%02x\n", ext_csd[141]);
		seq_printf(s, "enh_size_mult|0           : 0x%02x\n", ext_csd[140]);
		seq_printf(s, "enh_start_addr|3          : 0x%02x\n", ext_csd[139]);
		seq_printf(s, "enh_start_addr|2          : 0x%02x\n", ext_csd[138]);
		seq_printf(s, "enh_start_addr|1          : 0x%02x\n", ext_csd[137]);
		seq_printf(s, "enh_start_addr|0          : 0x%02x\n", ext_csd[136]);
		/* A441: reserved [135] */
		seq_printf(s, "sec_bad_blk_mgmnt[134]    : 0x%02x\n", ext_csd[134]);
		/* A441: reserved [133:0] */
	}
	/* B45 */
	if (ext_csd_rev >= 6) {
		int j;

		seq_printf(s, "tcase_support[132]        : 0x%02x\n", ext_csd[132]);
		seq_printf(s, "prog_cid_csd_ddr_support  : 0x%02x\n",
				ext_csd[130]);

		seq_puts(s, "vendor_specific_field:\n");
		for (j = 127; j >= 64; j--) {
			seq_printf(s, " [%3d] 0x%02x ", j, ext_csd[j]);
			if (!(j % 4))
				seq_puts(s, "\n");
		}
		seq_printf(s, "native_sector_size[63]     : 0x%02x\n", ext_csd[63]);
		seq_printf(s, "use_native_sector[62]      : 0x%02x\n", ext_csd[62]);
		seq_printf(s, "data_sector_size[61]       : 0x%02x\n", ext_csd[61]);
		seq_printf(s, "ini_timeout_emu[60]        : 0x%02x\n", ext_csd[60]);
		seq_printf(s, "class_6_ctrl[59]           : 0x%02x\n", ext_csd[59]);
		seq_printf(s, "dyncap_needed[58]          : 0x%02x\n", ext_csd[58]);
		seq_printf(s, "exception_events_ctrl|1    : 0x%02x\n", ext_csd[57]);
		seq_printf(s, "exception_events_ctrl|0    : 0x%02x\n", ext_csd[56]);
		seq_printf(s, "exception_events_status|1  : 0x%02x\n",
				ext_csd[55]);
		seq_printf(s, "exception_events_status|0  : 0x%02x\n",
				ext_csd[54]);
		seq_printf(s, "ext_partition_attribute|1  : 0x%02x\n",
				ext_csd[53]);
		seq_printf(s, "ext_partition_attribute|0  : 0x%02x\n",
				ext_csd[52]);

		seq_puts(s, "context_conf:\n");
		for (j = 51; j >= 37; j--) {
			seq_printf(s, " [%3d] 0x%02x ", j, ext_csd[j]);
			if (!(j % 4))
				seq_puts(s, "\n");
		}
		seq_puts(s, "\n");
		seq_printf(s, "packed_command_status[36]  : 0x%02x\n", ext_csd[36]);
		seq_printf(s, "packed_failure_index[35]   : 0x%02x\n", ext_csd[35]);
		seq_printf(s, "power_off_notification[34] : 0x%02x\n", ext_csd[34]);
		seq_printf(s, "cache_ctrl[33]             : 0x%02x\n", ext_csd[33]);
		seq_printf(s, "flush_cache[32]            : 0x%02x\n", ext_csd[32]);
		/*Reserved [31:0] */
		seq_printf(s, "pre_loading_data_size[25-22]		: 0x%08x\n",
				(ext_csd[25] << 24) |
				(ext_csd[24] << 16) | (ext_csd[23] << 8)  |
				ext_csd[22]);

		seq_printf(s, "max_pre_loading_data_size[21-18]	: 0x%08x\n",
				(ext_csd[21] << 24) |
				(ext_csd[20] << 16) | (ext_csd[19] << 8)  |
				ext_csd[18]);
	}

out_free:
	kfree(ext_csd);
	return err;
}

static int mmc_ext_csd_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_ext_csd_read, inode->i_private);
}

static const struct file_operations mmc_dbg_ext_csd_fops = {
	.open		= mmc_ext_csd_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void mmc_add_card_debugfs(struct mmc_card *card)
{
	struct mmc_host	*host = card->host;
	struct dentry	*root;

	if (!host->debugfs_root)
		return;

	root = debugfs_create_dir(mmc_card_id(card), host->debugfs_root);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err;

	card->debugfs_root = root;

	if (!debugfs_create_x32("state", S_IRUSR, root, &card->state))
		goto err;

	if (mmc_card_mmc(card) || mmc_card_sd(card))
		if (!debugfs_create_file("status", S_IRUSR, root, card,
					&mmc_dbg_card_status_fops))
			goto err;

	if (mmc_card_mmc(card))
		if (!debugfs_create_file("ext_csd", S_IRUSR, root, card,
					&mmc_dbg_ext_csd_fops))
			goto err;

	return;

err:
	debugfs_remove_recursive(root);
	card->debugfs_root = NULL;
	dev_err(&card->dev, "failed to initialize debugfs\n");
}

void mmc_remove_card_debugfs(struct mmc_card *card)
{
	debugfs_remove_recursive(card->debugfs_root);
}
