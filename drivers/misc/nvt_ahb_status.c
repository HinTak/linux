#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/bug.h>

struct ahb_status_t {
	void __iomem    *vbase;
};

static irqreturn_t ahb_status_isr(int irq, void *dev_id)
{
	struct ahb_status_t *ahb_status = (struct ahb_status_t *)dev_id;

	printk("+++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	printk("** ahb status = 0x%x **\n", readl(ahb_status->vbase));
	printk("+++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

	/* currently, we only handle ahb decode error,
	   clear its irq status to prevent more interrupt */
	writel(readl(ahb_status->vbase) & (~(1<<24)), ahb_status->vbase);

	/* it's a pity...we should never be here... */
	BUG();

	return IRQ_HANDLED;
}

static int __init ahb_status_probe(struct platform_device *pdev)
{
	struct ahb_status_t *ahb_status = NULL;
	struct resource *res = NULL;
	int err = 0;
	int irq = 0;

	printk("** in %s **\n", __func__);

	ahb_status = kmalloc(sizeof(struct ahb_status_t), GFP_KERNEL);
	if (!ahb_status) {
		err = -ENXIO;
		goto fail_malloc;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		err = -ENXIO;
		goto fail_no_mem_resource;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		err = -EBUSY;
		goto fail_no_mem_resource;
	}

	ahb_status->vbase = ioremap(res->start, resource_size(res));
	if (!ahb_status->vbase) {
		err = -ENXIO;
		goto fail_no_ioremap;
	}

	printk("*** ahb pyhs = 0x%x, vbase = 0x%x **\n", res->start, (unsigned int)ahb_status->vbase);
	
	irq = platform_get_irq(pdev, 0);
	if (request_irq(irq, ahb_status_isr, IRQF_DISABLED, "ahb-status", ahb_status)) {
		err = -ENXIO;
		goto fail_irq_request;
	}

	printk("*** ahb irq = %d ***\n", irq);

	return 0;

fail_irq_request:
	iounmap(ahb_status->vbase);
fail_no_ioremap:
	release_mem_region(res->start, resource_size(res));
fail_no_mem_resource:
	kfree(ahb_status);
fail_malloc:
	return err;
}

static int __exit ahb_status_remove(struct platform_device *pdev)
{
#if 0
	struct ep93xx_pwm *pwm = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ep93xx_pwm_disable(pwm);
	clk_disable(pwm->clk);
	clk_put(pwm->clk);
	platform_set_drvdata(pdev, NULL);
	sysfs_remove_group(&pdev->dev.kobj, &ep93xx_pwm_sysfs_files);
	iounmap(pwm->mmio_base);
	release_mem_region(res->start, resource_size(res));
	kfree(pwm);
	ep93xx_pwm_release_gpio(pdev);
#endif
	return 0;
}

static struct platform_driver ahb_status_driver = {
	.driver		= {
		.name	= "ahb_status_nvt",
		.owner	= THIS_MODULE,
	},
	.remove		= __exit_p(ahb_status_remove),
};

static int __init ahb_status_init(void)
{
	return platform_driver_probe(&ahb_status_driver, ahb_status_probe);
}

static void __exit ahb_status_exit(void)
{
	platform_driver_unregister(&ahb_status_driver);
}

module_init(ahb_status_init);
module_exit(ahb_status_exit);

MODULE_AUTHOR("Novatek Inc.");
MODULE_LICENSE("GPL");
