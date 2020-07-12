#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <soc/nvt/soc.h>
#include <linux/cpe.h>

enum nvt_chip_id
{
	NVT_CHIPID_NOT_READY,
	NVT_CHIP_UNKNOWN,
	NVT_KANT_S,
	NVT_KANT_S2,
	NVT_KANT_SU,
	NVT_KANT_SU2,
};

static enum nvt_chip_id nvt_chip_type = NVT_CHIPID_NOT_READY;

/* nvt proc entries - read only, persistent */
struct nvt_proc_entry {
	char name[24];
	int (*proc_read)(struct seq_file *m, void *v);
	void *private_data;
};

static int __init nvt_chip_type_init(void)
{
	u32 chip_id;
	u32 *remap_address;

	remap_address = ioremap(0xfd020100, 0x4);
	if (!remap_address) {
		printk(KERN_ERR "[%s] error! fail to map chip id address 0xfd020100, chip id function will not work  \n", __func__);
		nvt_chip_type = NVT_CHIP_UNKNOWN;
		return 0;
	}
	chip_id = *remap_address & 0xfff;
	/* printk(KERN_ERR "[%s]  raw 0x%x  chipid 0x%x\n", __func__ , *remap_address , chip_id); */
	iounmap(remap_address);

	switch (chip_id)
	{
	case 0x172:
		/* kant-s/s2 */
		/* katn-s2 has no kgd, pkgtype == 6 */
		if (ntcpe_get_pkgtype() == 0x6)
			nvt_chip_type = NVT_KANT_S2;
		else
			nvt_chip_type =NVT_KANT_S;
		break;
	case 0x673:
		/* kant-su */
		nvt_chip_type = NVT_KANT_SU;
		break;
	case 0x671:
		/* kant-su2 */
		nvt_chip_type = NVT_KANT_SU2;
		break;
	default:
		nvt_chip_type = NVT_CHIP_UNKNOWN;
		printk(KERN_ERR "[%s] unknown chip id 0x%x, chip id function will not work  \n", __func__, chip_id);
		break;
	}

	/*	printk(KERN_ERR "[%s] debug~~~~~~~~~~~~~~~~~~~~~~~nvt_chip_type %d\n", __func__, nvt_chip_type);*/
	return 0;
}

/* take care, return 1 means right, return 0 means wrong */
int soc_is_kants(void)
{
	if (nvt_chip_type == NVT_KANT_S)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(soc_is_kants);

int soc_is_kants2(void)
{
	if (nvt_chip_type == NVT_KANT_S2)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(soc_is_kants2);

int soc_is_kantsu(void)
{
	if (nvt_chip_type == NVT_KANT_SU)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(soc_is_kantsu);

int soc_is_kantsu2(void)
{
	if (nvt_chip_type == NVT_KANT_SU2)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(soc_is_kantsu2);

static int nvt_proc_chip_name(struct seq_file *m, void *v)
{
	switch (nvt_chip_type)
	{
	case NVT_KANT_S:
		seq_printf(m, "kant-s\n");
		break;
	case NVT_KANT_S2:
		seq_printf(m, "kant-s2\n");
		break;
	case NVT_KANT_SU:
		seq_printf(m, "kant-su\n");
		break;
	case NVT_KANT_SU2:
		seq_printf(m, "kant-su2\n");
		break;
	default:
		seq_printf(m, "unknown\n");
		break;
	}
	return 0;
}


static struct nvt_proc_entry nvt_proc_entries[] = {
	{
		.name		= "chip_name",
		.proc_read	= nvt_proc_chip_name,
	},
};

static int nvt_proc_open(struct inode *inode, struct file *file)
{
	struct nvt_proc_entry *nvt_entry = PDE_DATA(inode);
	return single_open(file, nvt_entry->proc_read, nvt_entry);
}


static const struct file_operations nvt_proc_file_ops = {
	.open = nvt_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


static int __init nvt_procfs_init(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nvt_proc_entries); i++) {
		struct nvt_proc_entry *e = &nvt_proc_entries[i];
		struct proc_dir_entry *entry = proc_create_data(
				e->name, 0644, NULL, &nvt_proc_file_ops, e);
		if (entry)
			pr_info ("/proc/%s registered.\n", e->name);
		else
			pr_err ("/proc/%s registration failed.\n", e->name);
	}
	return 0;
}

late_initcall(nvt_procfs_init);
postcore_initcall(nvt_chip_type_init);
