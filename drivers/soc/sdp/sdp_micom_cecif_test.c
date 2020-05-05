/*
 * SDP Micom CEC interface driver header(shared from micom)
 *
 * Copyright (C) 2017 dongseok lee <drain.lee@samsung.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <soc/sdp/sdp_micom_cecif.h>

#define CEC_TEST_NAME "cec-test"

struct cectest_t
{
	struct device *dev;
	struct sdp_mc_cecif_dev *smcec;

	/* debugfs */
	struct dentry *dbgfs_root;
};

int cectest_eventcb(u32 event, struct cec_msg *rxmsg, void *args)
{
	struct cectest_t *ct = args;

	dev_info(ct->dev, "event=0x%08x, rxmsg=%p, args=%p\n", event, rxmsg, args);
	if(rxmsg) {
		dev_info(ct->dev, "rx data=[%*ph]\n", rxmsg->len, rxmsg->msg);
	}

	return 0;
}


/****************************** debugfs ***************************************/
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

/* read file operation */
static int debugfs_cectest_get(void *data, u64 *val)
{
	struct cectest_t *ct = data;
	struct sdp_mc_cecif_dev *smcec = ct->smcec;
	int ret = 0;

	if(!ct->smcec) {
		ret = sdp_mc_cecif_of_device_attach(ct->dev, cectest_eventcb, ct, &smcec);
		if(ret < 0) {
			return ret;
		}
		ct->smcec = smcec;
	}

	ret = sdp_mc_cecif_enable(smcec, true);
	if(ret < 0) {
		return ret;
	}

	ret = sdp_mc_cecif_log_addr(smcec, 0x0);
	if(ret < 0) {
		return ret;
	}

	*val = 0;
	return 0;
}

/* write file operation */
static int debugfs_cectest_set(void *data, u64 val)
{
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(debugfs_cectest, debugfs_cectest_get, debugfs_cectest_set,
	"%llu\n");

static void cectest_add_debugfs(struct device *dev)
{
	struct cectest_t *ct = NULL;
	struct dentry *root;

	ct = dev_get_drvdata(dev);

	root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	ct->dbgfs_root = root;

	debugfs_create_file("test", 0644, root, ct, &debugfs_cectest);
	//debugfs_create_u32("callback_timeout_us", S_IRUGO|S_IWUGO, root, &ct->callback_timeout_us);
	//debugfs_create_u32("recv_timeout_ms", S_IRUGO|S_IWUGO, root, &ct->recv_timeout_ms);

	return;

err_root:
	ct->dbgfs_root = NULL;
	return;
}
#endif


static int cectest_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cectest_t *ct = NULL;

	ct = devm_kzalloc(&pdev->dev, sizeof(*ct), GFP_KERNEL);
	if (!ct)
		return -ENOMEM;

	ct->dev = dev;
	platform_set_drvdata(pdev, ct);

#ifdef CONFIG_DEBUG_FS
	cectest_add_debugfs(dev);
#endif

	dev_info(dev, "successfully probed\n");

	return 0;
}

static int cectest_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id cectest_match[] = {
	{
		.compatible	= "samsung,sdp-mc-cecif-test",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cectest_match);

static struct platform_driver cectest_pdrv = {
	.probe	= cectest_probe,
	.remove	= cectest_remove,
	.driver	= {
		.name		= CEC_TEST_NAME,
		.of_match_table	= cectest_match,
	},
};
module_platform_driver(cectest_pdrv);

MODULE_AUTHOR("Dongseok Lee <drain.lee@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung SDP Micom CECIF Test Module");

