#ifndef __MACH_CT_CA9X4_H
#define __MACH_CT_CA9X4_H

//#define WDT_TIMER_BE_TICK  

/*
 * Core IDs
 */
#define V2M_CT_ID_CA9		0x00072668
struct ct_desc {
	u32			id;
	const char		*name;
	void			(*map_io)(void);
	void			(*init_early)(void);
	void			(*init_irq)(void);
	void			(*init_tile)(void);
#ifdef CONFIG_SMP
	void			(*init_cpu_map)(void);
	void			(*smp_enable)(unsigned int);
#endif
};

extern struct ct_desc *ct_desc;

extern struct ct_desc ct_ca9x4_desc;


#endif
