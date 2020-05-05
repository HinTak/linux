void nvt_dt_smp_map_io(void);

extern struct smp_operations	nvt_ca9_smp_ops;

extern void nvt_ca9_cpu_die(unsigned int cpu);

#define A9_MPCORE_SCU_OFFSET		   (0x0000)
#define A9_MPCORE_GIC_CPU_OFFSET	 (0x0100)
#define A9_MPCORE_GIT_OFFSET		   (0x0200)
#define A9_MPCORE_TWD_OFFSET		   (0x0600)
#define A9_MPCORE_GIC_DIST_OFFSET  (0x1000)

#define CLK_EN_OFF	(0x110)
#define CLK_DIS_OFF	(0x114)

#define S_ARM3_DBG_RST	(19)
#define M_ARM3_DBG_RST	(0x1 << S_ARM3_DBG_RST)
#define S_ARM2_DBG_RST	(18)
#define M_ARM2_DBG_RST	(0x1 << S_ARM2_DBG_RST)
#define S_ARM1_DBG_RST	(17)
#define M_ARM1_DBG_RST	(0x1 << S_ARM1_DBG_RST)
#define S_ARM0_DBG_RST	(16)
#define M_ARM0_DBG_RST	(0x1 << S_ARM0_DBG_RST)
#define S_NEON3_CLKOFF	(15)
#define M_NEON3_CLKOFF	(0x1 << S_NEON3_CLKOFF)
#define S_NEON3_RST		(14)
#define M_NEON3_RST		(0x1 << S_NEON3_RST)
#define S_NEON2_CLKOFF	(13)
#define M_NEON2_CLKOFF	(0x1 << S_NEON2_CLKOFF)
#define S_NEON2_RST		(12)
#define M_NEON2_RST		(0x1 << S_NEON2_RST)
#define S_NEON1_CLKOFF	(11)
#define M_NEON1_CLKOFF	(0x1 << S_NEON1_CLKOFF)
#define S_NEON1_RST		(10)
#define M_NEON1_RST		(0x1 << S_NEON1_RST)
#define S_ARM3_CLKOFF	(7)
#define M_ARM3_CLKOFF	(0x1 << S_ARM3_CLKOFF)
#define S_ARM3_RST		(6)
#define M_ARM3_RST		(0x1 << S_ARM3_RST)
#define S_ARM2_CLKOFF	(5)
#define M_ARM2_CLKOFF	(0x1 << S_ARM2_CLKOFF)
#define S_ARM2_RST		(4)
#define M_ARM2_RST		(0x1 << S_ARM2_RST)
#define S_ARM1_CLKOFF	(3)
#define M_ARM1_CLKOFF	(0x1 << S_ARM1_CLKOFF)
#define S_ARM1_RST		(2)
#define M_ARM1_RST		(0x1 << S_ARM1_RST)


static inline unsigned long get_periph_base(void)
{
	unsigned long val;
	asm("mrc p15, 4, %0, c15, c0, 0 @ get CBAR"
	  : "=r" (val) : : "cc");
	return val;
}
