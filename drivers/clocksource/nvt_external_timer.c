
#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>

struct nvt_timer_struct {
	void __iomem		*regs;		/* device memory/io */
	char				timer_name[20];
	int					irq;
	u32					overflow_count;
	struct clocksource nvt_timer_clocksource;
	spinlock_t nvt_timer_lock;
} nvt_timer_data;

void __iomem *nvt_timer_reg_base;
EXPORT_SYMBOL_GPL(nvt_timer_reg_base);

/*
	24 bits timer
	2^24 = 16777216
	rate :  10^8
	divisor = 128 => 2^7 => 0111 = 7
	resolution : 1.28us;
	reload interval = 21.47483648s


	divisor = 64 => 2^6 => 0110 = 6
	resolution : 0.64us;
	reload interval = 10.73741824s

*/

/*
static u64 nvt_timer2ns(void)
{
	unsigned long flags;
	u32 local_overflow;
	u64	raw_timer;

	spin_lock_irqsave(&nvt_timer_data.nvt_timer_lock, flags);
	raw_timer = readl(nvt_timer_data.regs);
	local_overflow = nvt_timer_data.overflow_count;
	spin_unlock_irqrestore(&nvt_timer_data.nvt_timer_lock, flags);

	raw_timer = (local_overflow * (1 << 24) +
		((1 << 24) - raw_timer)) * 640;
	return raw_timer;
}

*/
/*
static struct clocksource nvt_timer_clocksource = {
	.name	= "nvt_timer",
	.rating	= 300,
	.read	= nvt_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(24),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};
*/

/*
static irqreturn_t nvt_timer_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	spin_lock_irqsave(&nvt_timer_data.nvt_timer_lock, flags);
	nvt_timer_data.overflow_count++;
	spin_unlock_irqrestore(&nvt_timer_data.nvt_timer_lock, flags);

	pr_info("[TIMER] %s irq %d\n", __func__, irq);

	return IRQ_HANDLED;
}
*/

static u64 nvt_read_raw_timer(void)
{
	return (1 << 24) - readl(nvt_timer_data.regs);
}

static cycle_t nvt_clocksource_read(struct clocksource *cs)
{
	return nvt_read_raw_timer();
}


static void __init nvt_timer_of_register(struct device_node *np)
{
	struct resource res;
	int ret;
	unsigned int irq;
	static int only;

	if (only) {
		pr_info("[TIMER] only allow register one nvt timer clock source\n");
		return;
	}
	only = 1;

	pr_info("[TIMER] %s %s\n", np->name, np->full_name);

	if (strlen(np->name) > 19) {
		pr_err("[TIMER] device tree name is too long, %d\n",
			strlen(np->name));
		return;
	}

	nvt_timer_data.overflow_count = 0;
	spin_lock_init(&nvt_timer_data.nvt_timer_lock);
	/* timer name */
	strcpy(nvt_timer_data.timer_name, np->name);

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_err("[TIMER] fail to get regs from dt, ret %d\n", ret);
		return;
	}

	nvt_timer_reg_base = nvt_timer_data.regs
			= ioremap(res.start, resource_size(&res));
	if (!nvt_timer_data.regs) {
		pr_err("[TIMER] fail to do ioremap\n");
		return;
	}
/*
    irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		pr_err("[TIMER] fail to get irq from dt\n");
		return;
	}
*/
	ret = of_property_read_u32(np, "nvt,irq", &irq);
	if (ret) {
		pr_err("[TIMER] fail to get timer irq, %d\n", ret);
		iounmap(nvt_timer_data.regs);
		return;
	}
/*
	pr_info("[TIMER] addr 0x%x iomap addr 0x%p irq %u\n",
		res.start, nvt_timer_data.regs, irq);
*/
/*
	ret = request_irq(irq, nvt_timer_interrupt,
		IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
		"nvt_timer", NULL);
	if (ret) {
		pr_err("[TIMER] fail to request irq\n");
		iounmap(nvt_timer_data.regs);
		return;
	}
*/
	nvt_timer_data.nvt_timer_clocksource.name = nvt_timer_data.timer_name;
	/* now arm global timer is 300 */
	nvt_timer_data.nvt_timer_clocksource.rating = 200;
	nvt_timer_data.nvt_timer_clocksource.mask = CLOCKSOURCE_MASK(24);
	nvt_timer_data.nvt_timer_clocksource.read = nvt_clocksource_read;
	nvt_timer_data.nvt_timer_clocksource.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	/* enable timer  */
	writel(0xc6, nvt_timer_data.regs + 0x4);
	/* 1s / 0.64us = 1562500  */
	sched_clock_register(nvt_read_raw_timer, 24, 1562500);
	clocksource_register_hz(&nvt_timer_data.nvt_timer_clocksource, 1562500);

}

CLOCKSOURCE_OF_DECLARE(arm_gt, "nvt,nvt72xxx-timer",
			nvt_timer_of_register);
