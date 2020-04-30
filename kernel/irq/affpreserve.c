#include <linux/interrupt.h>
#include <linux/irq.h>

#include "internals.h"

#ifdef CONFIG_PRESERVE_IRQ_AFFINITY
static struct cpumask affinity_backup[NR_IRQS] = {CPU_MASK_NONE};

void backup_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	if (irq >= NR_IRQS)
		return;

	cpumask_copy(&affinity_backup[irq], mask);
}

static void restore_one_irq_affinity(unsigned int cpu,
			unsigned int irq, struct irq_desc *desc)
{
	struct irq_data *data;
	struct irq_chip *chip;
	struct cpumask new_cpumask;

	data = irq_desc_get_irq_data(desc);
	chip = irq_data_get_irq_chip(data);

	if (!chip->irq_set_affinity)
		return;

	if (!cpumask_test_cpu(cpu, &affinity_backup[data->irq]))
		return;

	cpumask_and(&new_cpumask, &affinity_backup[data->irq], cpu_online_mask);

	irq_do_set_affinity(data, &new_cpumask, true, false);

}

void local_restore_irq_affinities(void)
{
	unsigned int irq;
	struct irq_desc *desc;
	unsigned int cpu = smp_processor_id();

	for_each_irq_desc(irq, desc) {
		raw_spin_lock(&desc->lock);
		restore_one_irq_affinity(cpu, irq, desc);
		raw_spin_unlock(&desc->lock);
	}
}
#endif /* #ifdef CONFIG_PRESERVE_IRQ_AFFINITY */
