/*
* Copyright (C) 2013 Samsung Electronics Co.Ltd
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#define SDP_MICOM_PINCTRL_VERSION "20160708(porting for linux4.1)"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <soc/sdp/soc.h>

static DEFINE_MUTEX(gpio_lock);

/* register offsets */
#define SDP_GPIO_CON		0x0
#define SDP_GPIO_PWDATA		0x4
#define SDP_GPIO_PRDATA		0x8

struct sdp_gpio_bank {
	struct sdp_mc_pincrtl_data *pinctrl;
	struct gpio_chip	chip;
	char			label[16];
	unsigned int		nr_gpios;
	unsigned int		reg_offset;
	spinlock_t 			port_lock;
	unsigned long		output;
};

struct sdp_mc_pincrtl_data {
	void __iomem		*base;
	struct sdp_gpio_bank	*gpio_bank;
	unsigned int		nr_banks;
	unsigned int		start_offset;
	unsigned int		index_base;

	struct pinctrl_desc	*ctrldesc;
	/* TODO */
};

#define SDP_GPIO_BANK(name, nr, offset)		\
	{					\
		.label		= name,		\
		.nr_gpios	= nr,		\
		.reg_offset	= offset,	\
	}

static struct sdp_gpio_bank *sdp_gpio_banks;

#define MAX_CHIP_NUM 2
static int num_chip;
static u32 chip_sel[MAX_CHIP_NUM] = { 1, 1,};
static struct sdp_mc_pincrtl_data *sdp_pinctrl[MAX_CHIP_NUM];

static struct gpio_save
{
	void __iomem * start;
	u32 size;
}gpio_save_t[MAX_CHIP_NUM];

static inline struct sdp_gpio_bank *chip_to_bank(struct gpio_chip *chip)
{
	return container_of(chip, struct sdp_gpio_bank, chip);
}

static void sdp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u8 data;

	spin_lock_irqsave(&gpio_bank->port_lock, flags);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;
	data = readl(reg + SDP_GPIO_PWDATA);
	data &= ~(1 << offset);
	if (value)
		data |= 1 << offset;
	writel(data, reg + SDP_GPIO_PWDATA);

	spin_unlock_irqrestore(&gpio_bank->port_lock, flags);

}

static int sdp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	void __iomem *reg;
	u8 data;

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	if (test_bit(offset, &gpio_bank->output))
		data = readb(reg + SDP_GPIO_PWDATA);
	else
		data = readb(reg + SDP_GPIO_PRDATA);
	data >>= offset;
	data &= 1;

	return data;
}

static int sdp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u32 data;

	__clear_bit(offset, &gpio_bank->output);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	spin_lock_irqsave(&gpio_bank->port_lock, flags);

	data = readl(reg);
	data &= ~(0x3 << (offset * 4));
	data |= 0x2 << (offset * 4);

	writel(data, reg);

	spin_unlock_irqrestore(&gpio_bank->port_lock, flags);

	return 0;

	/*
	* TODO
	* return pinctrl_gpio_direction_input(chip->base + offset);
	*/
}

static int sdp_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct sdp_gpio_bank *gpio_bank = chip_to_bank(chip);
	unsigned long flags;
	void __iomem *reg;
	u32 data;

	__set_bit(offset, &gpio_bank->output);
	sdp_gpio_set(chip, offset, value);

	reg = gpio_bank->pinctrl->base + gpio_bank->reg_offset;

	spin_lock_irqsave(&gpio_bank->port_lock, flags);

	data = readl(reg);
	data |= 0x3 << (offset * 4);

	writel(data, reg);

	spin_unlock_irqrestore(&gpio_bank->port_lock, flags);

	return 0;

	/*
	* TODO
	* return pinctrl_gpio_direction_output(chip->base + offset);
	*/
}

static const struct gpio_chip sdp_gpio_chip = {
	.set = sdp_gpio_set,
	.get = sdp_gpio_get,
	.direction_input = sdp_gpio_direction_input,
	.direction_output = sdp_gpio_direction_output,
	.owner = THIS_MODULE,
};

static int sdp_gpiolib_register(struct platform_device *pdev, struct sdp_mc_pincrtl_data *pinctrl)
{
	struct sdp_gpio_bank *gpio_bank;
	struct gpio_chip *chip;
	int cnt = 0;
	int ret;
	int i;
	unsigned int reg_offset = pinctrl->start_offset;
	unsigned int base = pinctrl->index_base;

	gpio_bank = pinctrl->gpio_bank + cnt;
	for (i = 0; i < (int)pinctrl->nr_banks; i++, gpio_bank++, cnt++) {
		struct device_node *node = pdev->dev.of_node;
		struct device_node *np;

		spin_lock_init(&gpio_bank->port_lock);

		gpio_bank->chip = sdp_gpio_chip;
		gpio_bank->pinctrl = pinctrl;
		gpio_bank->reg_offset = reg_offset;
		gpio_bank->nr_gpios = 8;

		chip = &gpio_bank->chip;
		chip->dev = &pdev->dev;
		chip->base = base;
		chip->ngpio = (u16)gpio_bank->nr_gpios;
		snprintf(gpio_bank->label, 16, "gpio_mc%d", cnt);

		chip->label = gpio_bank->label;

		for_each_child_of_node(node, np) {
			if (!of_find_property(np, "gpio-controller", NULL))
				continue;
			if (!strcmp(gpio_bank->label, np->name)) {
				chip->of_node = np;
				break;
			}
		}

		ret = gpiochip_add(chip);
		if (ret) {
			dev_err(&pdev->dev, "failed to register gpio_chip\n");
			goto err;
		}

		base += 10;//gpio_bank->nr_gpios;
		reg_offset += 0xC;
	}

	return 0;

err:
	for (--i, --gpio_bank; i >= 0; i--, gpio_bank--)
		gpiochip_remove(&gpio_bank->chip);

	return ret;
}

static void sdp_gpiolib_unregister(struct platform_device *pdev, struct sdp_mc_pincrtl_data *pinctrl)
{
	struct sdp_gpio_bank *gpio_bank = pinctrl->gpio_bank;
	u32 i;

	for (i = 0; i < pinctrl->nr_banks; i++, gpio_bank++) {
		gpiochip_remove(&gpio_bank->chip);

	}
}

static struct pinctrl_desc sdp_mc_pincrtl_desc = {
	/* TODO */
};

static int sdp_mc_pincrtl_register(struct platform_device *pdev, struct sdp_mc_pincrtl_data *pinctrl)
{
	/* TODO */
	return 0;
}

static void sdp_mc_pincrtl_unregister(struct platform_device *pdev, struct sdp_mc_pincrtl_data *pinctrl)
{
	struct device *dev = &pdev->dev;
	/* TODO */
	dev_info(dev, "[%d] %s\n", __LINE__, __func__);
}

static int sdp_mc_pincrtl_check_platfrom(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (of_property_read_u32_array(dev->of_node, "model-sel", chip_sel, num_chip) == 0)
	{
	}

	return 0;
}

static int sdp_mc_pincrtl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret, i;
	u32 total_bank = 0;
	u32 *value;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENXIO;
	}

	for (i = 0; i < MAX_CHIP_NUM; i++)
	{
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			if (i == 0)dev_err(dev, "cannot find IO resource\n");

			break;
		}

		sdp_pinctrl[i] = devm_kzalloc(dev, sizeof(struct sdp_mc_pincrtl_data), GFP_KERNEL);
		if (!sdp_pinctrl[i]) {
			dev_err(dev, "failed to allocate memory\n");
			return -ENOMEM;
		}

		sdp_pinctrl[i]->base = devm_ioremap_resource(&pdev->dev, res);

		if (!sdp_pinctrl[i]->base) {
			dev_err(dev, "ioremap failed\n");
		}

		gpio_save_t[i].start = sdp_pinctrl[i]->base;
		gpio_save_t[i].size = (u32)res->end - res->start;
	}

	num_chip = i;

	value = devm_kzalloc(dev, sizeof(u32)*num_chip, GFP_KERNEL);
	if (of_property_read_u32_array(dev->of_node, "nr-banks", value, num_chip))
	{

	}
	for (i = 0; i < num_chip; i++)
	{
		sdp_pinctrl[i]->nr_banks = value[i];
		total_bank += sdp_pinctrl[i]->nr_banks;
	}

	if (of_property_read_u32_array(dev->of_node, "start-offset", value, num_chip))
	{

	}
	for (i = 0; i < num_chip; i++)
	{
		sdp_pinctrl[i]->start_offset = value[i];
	}

	if (of_property_read_u32_array(dev->of_node, "index-base", value, num_chip))
	{
	}
	for (i = 0; i < num_chip; i++)
	{
		sdp_pinctrl[i]->index_base = value[i];
	}

	sdp_gpio_banks = devm_kzalloc(dev, sizeof(struct sdp_gpio_bank) * (total_bank), GFP_KERNEL);
	if (!sdp_gpio_banks) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	for (i = 0; i < num_chip; i++)
	{
		dev_info(dev, "#%d base 0x%08x, offset 0x%x, nr_banks %d", i, (u32)sdp_pinctrl[i]->base, sdp_pinctrl[i]->start_offset, sdp_pinctrl[i]->nr_banks);
		sdp_pinctrl[i]->gpio_bank = sdp_gpio_banks;
		sdp_pinctrl[i]->ctrldesc = &sdp_mc_pincrtl_desc;


		ret = sdp_gpiolib_register(pdev, sdp_pinctrl[i]);
		if (ret)
			return ret;
	}

	ret = sdp_mc_pincrtl_register(pdev, sdp_pinctrl[i]);
	if (ret) {
		sdp_gpiolib_unregister(pdev, sdp_pinctrl[i]);
		return ret;
	}
	platform_set_drvdata(pdev, sdp_pinctrl);
	sdp_mc_pincrtl_check_platfrom(pdev);

	return 0;
}

static int sdp_mc_pincrtl_remove(struct platform_device *pdev)
{
	struct sdp_mc_pincrtl_data *pinctrl = platform_get_drvdata(pdev);

	sdp_gpiolib_unregister(pdev, pinctrl);
	sdp_mc_pincrtl_unregister(pdev, pinctrl);

	return 0;
}

#ifdef CONFIG_PM
static int sdp_mc_pincrtl_suspend(struct device *dev)
{
	return 0;
}

static int sdp_mc_pincrtl_resume(struct device *dev)
{
	return 0;
}
#endif
static const struct of_device_id sdp_mc_pincrtl_dt_match[] = {
	{ .compatible = "samsung,sdp-micom-pinctrl", },
	{},
};
#ifdef CONFIG_PM
static const struct dev_pm_ops sdp_mc_pincrtl_pm_ops = {
	.suspend_late = sdp_mc_pincrtl_suspend,
	.resume_early = sdp_mc_pincrtl_resume,
	//.suspend = sdp_mc_pincrtl_suspend,
	//.resume = sdp_mc_pincrtl_resume,
};
#define sdp_mc_pincrtl_PM_OPS (&sdp_mc_pincrtl_pm_ops)
#else /* !CONFIG_PM */
#define sdp_mc_pincrtl_PM_OPS NULL
#endif /* !CONFIG_PM */

MODULE_DEVICE_TABLE(of, sdp_mc_pincrtl_dt_match);

static struct platform_driver sdp_mc_pincrtl_driver = {
	.probe = sdp_mc_pincrtl_probe,
	.remove = sdp_mc_pincrtl_remove,
	.driver = {
		.name = "sdp-micom-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sdp_mc_pincrtl_dt_match),
#ifdef CONFIG_PM
		.pm = sdp_mc_pincrtl_PM_OPS,
#endif
	},
};

static int __init sdp_mc_pincrtl_init(void)
{
	pr_info("%s: driver registered, ver%s", sdp_mc_pincrtl_driver.driver.name, SDP_MICOM_PINCTRL_VERSION);
	return platform_driver_register(&sdp_mc_pincrtl_driver);
}
postcore_initcall(sdp_mc_pincrtl_init);

static void __exit sdp_mc_pincrtl_exit(void)
{
	platform_driver_unregister(&sdp_mc_pincrtl_driver);
}
module_exit(sdp_mc_pincrtl_exit);

MODULE_DESCRIPTION("Samsung SDP SoCs Micom pinctrl driver");
MODULE_LICENSE("GPL v2");