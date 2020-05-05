#include <linux/device.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

struct platform_device *nvt_ahb_status_dev(void);

/* we get controller's resource from device tree, so set each fields zero here */
static struct resource ahb_status_resources[] = {
	[0] = {
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ahb_status_dev = {
	.name			= "ahb_status_nvt",
	.id			= 0,
	.resource		= ahb_status_resources,
	.num_resources		= ARRAY_SIZE(ahb_status_resources),
};

struct platform_device *nvt_ahb_status_dev(void)
{
	struct device_node *node = NULL;

	node = of_find_compatible_node(NULL, NULL, "nvt,ahb-status");
	if (node) {
		/* get regs */
		of_address_to_resource(node, 0, &ahb_status_resources[0]);

		/* get irq */
		irq_of_parse_and_map(node, 0);
		ahb_status_resources[1].start = irq_of_parse_and_map(node, 0);
		ahb_status_resources[1].end = ahb_status_resources[1].start;
		of_node_put(node);
	} else {
		printk("*** [AHB] can not find ahb status device tree node.\n");
	}

	printk("*** [AHB] ctrl base = 0x%x, irq=%d\n", ahb_status_resources[0].start, ahb_status_resources[1].start);

	return &ahb_status_dev;
}
