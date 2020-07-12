/*******************************************************************************
 *	smem_test.c (Samsung DTV Soc SMEM Test driver)
 *	author : drain.lee@samsung.com
 ******************************************************************************/
//#define SMEM_TEST_VER_STR	"20170324(initial release)"
#define SMEM_TEST_VER_STR	"20170404(add get regsion info)"

#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <soc/sdp/smem.h>

/******************************** Defines *************************************/
#define SMEM_TEST_DRV_NAME	"smem-test"

struct smemtest_priv_t {
	struct device *dev;
	dev_t devt;
	struct dentry *dbgfs_root;
	struct smem_handle *last_smem_hnd;
	dma_addr_t last_dmaaddr;
	unsigned long last_size;
};

/******************************* Functions ************************************/
//#define USE_DUMMY
struct smemhandle_priv
{
	char	name[16];
};
static struct smem_handle* __smemtest_alloc_on_region_mask(struct device *dev,
		const char *name,
		size_t size, size_t align,
		smem_flags_t flags, u32 region_mask) {
#ifdef USE_DUMMY
	return kmalloc(0x1000, GFP_KERNEL);
#else
	struct smemhandle_priv *hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	struct smem_handle *handle;
	BUG_ON(!hpriv);
	strncpy(hpriv->name, name, sizeof(hpriv->name));
	hpriv->name[sizeof(hpriv->name) - 1] = '\0';

	//struct smem_handle* smem_alloc_on_region(struct device *dev, size_t size, size_t align, smem_flags_t flags, int region_idx);
	handle = smem_alloc_on_region_mask_data(dev, size, align, flags, region_mask, hpriv);
	if (!handle) {
		kfree(hpriv);
	}
	return handle;
#endif
}
static int  __smemtest_free(struct smem_handle *handle) {
#ifdef USE_DUMMY
	return kfree(handle);
#else
	//int  smem_free(struct smem_handle *handle);
	struct smemhandle_priv *hpriv = smem_get_private(handle);
	smem_free(handle);
	kfree(hpriv);
	return 0;
#endif
}

/******************************* trace ************************************/

static void noinline smemtest_trace_alloc(struct smem_handle *handle)
{
	phys_addr_t addr0 = smem_phys(handle);
	phys_addr_t addr1 = smem_phys(handle) + smem_size(handle);
	
	trace_printk("%02d %08x%08x - %08x%08x\n",
		smem_regionid(handle),
			(u32)(addr0 >> 32), (u32)(addr0 & 0xffffffff),
			(u32)(addr1 >> 32), (u32)(addr1 & 0xffffffff));
}

static void noinline smemtest_trace_free(struct smem_handle *handle)
{
	phys_addr_t addr0 = smem_phys(handle);
	phys_addr_t addr1 = smem_phys(handle) + smem_size(handle);
	
	trace_printk("%02d %08x%08x - %08x%08x\n",
		smem_regionid(handle),
			(u32)(addr0 >> 32), (u32)(addr0 & 0xffffffff),
			(u32)(addr1 >> 32), (u32)(addr1 & 0xffffffff));
}

static void noinline  smemtest_trace_info(struct device *dev)
{
	int nr_regions = smem_device_nr_regions(dev);
	int i;
	for (i = 0; i < nr_regions; i++) {
		phys_addr_t addr0, addr1;
		smem_region_range(dev, i, &addr0, &addr1);
		trace_printk("%02d %02d %08x%08x - %08x%08x\n",
			i, smem_device_regionid(dev, i),
			(u32)(addr0 >> 32), (u32)(addr0 & 0xffffffff),
			(u32)(addr1 >> 32), (u32)(addr1 & 0xffffffff));
	}
}

static void noinline smemtest_trace_alloc_fail(u32 region_mask, size_t size)
{
	trace_printk("%02x %08zx\n", region_mask, size);
}

/********************************* debugfs ************************************/
#ifdef CONFIG_DEBUG_FS

#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
#include <mach/sdp_hwclock.h>
#endif
static unsigned long long ___get_nsecs(void)
{
#ifdef CONFIG_USE_HW_CLOCK_FOR_TRACE
	return hwclock_ns((uint32_t *)hwclock_get_va());
#else
	return sched_clock();
#endif
}



static ssize_t dbgfs_smem_alloc_fops_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct smemtest_priv_t *priv = ((struct seq_file *)file->private_data)->private;
	struct simple_attr *attr;
	char *running, *token;
	size_t bufsize;
	ssize_t ret;
	char input_string[128];

	bufsize = min(sizeof(input_string) - 1, len);
	if (copy_from_user(input_string, buf, bufsize))
		goto out;
	input_string[bufsize] = '\0';

	//"test 0x100 0 0 0"
	//pr_info("ALLOC: %s", input_string);
	{
		const char *name = NULL;
		size_t size = 0x0;
		size_t align = 0;
		smem_flags_t flags = 0;
		u32 region_mask = -1;

		running = input_string;
		token = strsep(&running, " ");
		if(token == NULL) return -EINVAL;
		name = token;

		token = strsep(&running, " ");
		if(token == NULL) return -EINVAL;
		kstrtoll(token + 2, 16, (void *)&size);

		token = strsep(&running, " ");
		if(token == NULL) return -EINVAL;
		kstrtoll(token + 2, 16, (void *)&align);

		token = strsep(&running, " ");
		if(token == NULL) return -EINVAL;
		kstrtoll(token + 2, 16, (void *)&flags);

		token = strsep(&running, " ");
		if(token != NULL) {
			kstrtoul(token, 16, (void *)&region_mask);;
		}

		priv->last_smem_hnd = __smemtest_alloc_on_region_mask(priv->dev, name, size, align, flags, region_mask);
		if(priv->last_smem_hnd == NULL) {
			smemtest_trace_alloc_fail(region_mask, size);
			return -ENOMEM;
		} else {
			smemtest_trace_alloc(priv->last_smem_hnd);
		}

		priv->last_dmaaddr = smem_phys(priv->last_smem_hnd);
		priv->last_size = smem_size(priv->last_smem_hnd);
	}
out:
	return len;
}

static int dbgfs_smem_alloc_fops_show(struct seq_file *s, void *data) {
	struct smemtest_priv_t *priv = s->private;

	if(priv->last_smem_hnd == NULL) {
		//seq_printf(s, "0x0");
		return -ENOMEM;
	}

	seq_printf(s, "0x%p 0x%09llx 0x%08lx", priv->last_smem_hnd, (u64)priv->last_dmaaddr, priv->last_size);

	return 0;
}

static int dbgfs_smem_alloc_fops_open(struct inode *inode, struct file *file)
{
	single_open(file, dbgfs_smem_alloc_fops_show, inode->i_private);
	return 0;
}

static const struct file_operations dbgfs_smem_alloc_fops = {
	.owner		= THIS_MODULE,
	.open		= dbgfs_smem_alloc_fops_open,
	.read		= seq_read,
	.write		= dbgfs_smem_alloc_fops_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static ssize_t dbgfs_smem_free_fops_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct smemtest_priv_t *priv = ((struct seq_file *)file->private_data)->private;
	struct simple_attr *attr;
	u64 val;
	size_t bufsize;
	ssize_t ret;
	char input_string[128];

	bufsize = min(sizeof(input_string) - 1, len);
	if (copy_from_user(input_string, buf, bufsize))
		goto out;
	input_string[bufsize] = '\0';

	//pr_info("FREE: %s", input_string);

	{
		struct smem_handle *handle = NULL;

		kstrtoll(input_string + 2, 16, (void *)&handle);

		smemtest_trace_free(handle);
		__smemtest_free(handle);
	}
out:
	return len;
}

static int dbgfs_smem_free_fops_show(struct seq_file *s, void *data) {
	struct smemtest_priv_t *priv = s->private;

	//seq_printf(s, "0x%p\n", priv->last_smem_hnd);

	return 0;
}

static int dbgfs_smem_free_fops_open(struct inode *inode, struct file *file)
{
	single_open(file, dbgfs_smem_free_fops_show, inode->i_private);
	return 0;
}

static const struct file_operations dbgfs_smem_free_fops = {
	.owner		= THIS_MODULE,
	.open		= dbgfs_smem_free_fops_open,
	.read		= seq_read,
	.write		= dbgfs_smem_free_fops_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t dbgfs_smem_regioninfo_fops_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct smemtest_priv_t *priv = ((struct seq_file *)file->private_data)->private;
	struct simple_attr *attr;
	u64 val;
	size_t bufsize;
	ssize_t ret;
	char input_string[128];

	bufsize = min(sizeof(input_string) - 1, len);
	if (copy_from_user(input_string, buf, bufsize))
		goto out;
	input_string[bufsize] = '\0';

	{
		struct smem_handle *handle = NULL;

		kstrtoll(input_string + 2, 16, (void *)&handle);

		smemtest_trace_free(handle);
		__smemtest_free(handle);
	}
out:
	return len;
}

static int dbgfs_smem_regioninfo_fops_show(struct seq_file *s, void *data) {
	struct smemtest_priv_t *priv = s->private;
	int nr_regions = 0, idx;
	phys_addr_t min, max;


	nr_regions = smem_device_nr_regions(priv->dev);
	for(idx = 0; idx < nr_regions; idx++) {
		smem_region_range(priv->dev, idx, &min, &max);
		seq_printf(s, "%d 0x%09llx 0x%09llx 0x%09llx\n", idx, (u64)min, (u64)max, (u64)max-min + 1);
	}

	return 0;
}

static int dbgfs_smem_regioninfo_fops_open(struct inode *inode, struct file *file)
{
	single_open(file, dbgfs_smem_regioninfo_fops_show, inode->i_private);
	return 0;
}

static const struct file_operations dbgfs_smem_regioninfo_fops = {
	.owner		= THIS_MODULE,
	.open		= dbgfs_smem_regioninfo_fops_open,
	.read		= seq_read,
	.write		= dbgfs_smem_regioninfo_fops_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void smemtest_debugfs_create(struct smemtest_priv_t *priv) {
	struct dentry *root;

	root = debugfs_create_dir(SMEM_TEST_DRV_NAME, NULL);
	if (IS_ERR(root)) {
		/* Don't complain -- debugfs just isn't enabled */
		return;
	}
	if (!root) {
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		pr_err("failed to create the debugfs directory!\n");
		goto err_root;
	}

	priv->dbgfs_root = root;

	if(debugfs_create_file("alloc", S_IRUSR|S_IWUSR, root, priv, &dbgfs_smem_alloc_fops) == NULL) {
		pr_err("failed to create the debugfs alloc!\n");
		goto err_create;
	}

	if(debugfs_create_file("free", S_IRUSR|S_IWUSR, root, priv, &dbgfs_smem_free_fops) == NULL) {
		pr_err("failed to create the debugfs free!\n");
		goto err_create;
	}

	if(debugfs_create_file("regioninfo", S_IRUSR|S_IWUSR, root, priv, &dbgfs_smem_regioninfo_fops) == NULL) {
		pr_err("failed to create the debugfs regioninfo!\n");
		goto err_create;
	}

	pr_info("debugfs create done.\n");

	return;

err_create:
	debugfs_remove_recursive(root);

err_root:
	priv->dbgfs_root = NULL;

	return;
}
#endif

static void smemtest_debugfs_destroy(struct smemtest_priv_t *priv)
{
	debugfs_remove_recursive(priv->dbgfs_root);
}

/****************************** Platform Device ********************************/
static int smemtest_probe(struct platform_device *pdev) {
	struct smemtest_priv_t *priv = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	ret = smem_device_of_init(dev);
	if(ret < 0) {
		pr_err("%s: smem_device_of_init fail(%d)", SMEM_TEST_DRV_NAME, ret);
		return ret;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if(priv == NULL) {
		pr_err("can't allocate memory\n");
		ret = -ENOMEM;
		goto free_priv;
	}

	priv->dev = &pdev->dev;
	dev_info(priv->dev, "device probed!\n");

#ifdef CONFIG_DEBUG_FS
	smemtest_debugfs_create(priv);
#endif

	smemtest_trace_info(dev);

	platform_set_drvdata(pdev, priv);

	return 0;

free_priv:
	return ret;
}

static int smemtest_remove(struct platform_device *pdev)
{
	smemtest_debugfs_destroy(platform_get_drvdata(pdev));
	smem_device_detach(&pdev->dev);
	return 0;
}

static const struct of_device_id smemtest_dt_match[] = {
	{ .compatible = "samsung,sdp-smemtest", },
	{},
};
MODULE_DEVICE_TABLE(of, smemtest_dt_match);


static struct platform_driver smemtest_driver = {
	.probe		= smemtest_probe,
	.remove		= smemtest_remove,
	.driver = {
		.name	= SMEM_TEST_DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(smemtest_dt_match),
	},
};

/********************************** Module ************************************/
static struct class *smem_class;

static int __init smemtest_init(void) {

	pr_err(SMEM_TEST_DRV_NAME ": Registered driver. version %s\n", SMEM_TEST_VER_STR);
	return platform_driver_register(&smemtest_driver);
}
static void __exit smemtest_exit(void) {
	platform_driver_unregister(&smemtest_driver);
}
module_init(smemtest_init);
module_exit(smemtest_exit);

MODULE_DESCRIPTION("Samsung smem allocator test driver");
MODULE_AUTHOR("Dongsuk Lee <drain.lee@samsung.com>");
MODULE_LICENSE("GPL v2");
