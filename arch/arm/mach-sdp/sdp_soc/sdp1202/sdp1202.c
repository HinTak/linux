/*
 * sdp1202.c
 *
 * Copyright (C) 2012 Samsung Electronics.co
 * SeungJun Heo <seungjun.heo@samsung.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/tps54921.h>
#include <linux/regulator/sn1202033.h>
#include <linux/platform_data/sdp-hsotg.h>
#include <linux/platform_data/clk-sdp.h>
#include <linux/proc_fs.h>
#include <linux/cpu.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>

#include <mach/hardware.h>
#include <mach/sdp_soc.h>

#include <plat/sdp_spi.h> // use for struct sdp_spi_info and sdp_spi_chip_ops by drain.lee
#include <linux/spi/spi.h>

#include <plat/sdp_mmc.h>
#include <plat/sdp_tmu.h>

int sdp_get_revision_id(void);

extern unsigned int sdp_revision_id;
extern bool sdp_is_dual_mp;
extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

static struct map_desc sdp1202_io_desc[] __initdata = {
	[0] = {
		.virtual = VA_SFR0_BASE,
		.pfn     = __phys_to_pfn(SFR0_BASE),
		.length  = SFR0_SIZE,
		.type    = MT_DEVICE
	},
};

static struct resource sdp_uart0_resource[] = {
	[0] = {
		.start 	= PA_UART_BASE,
		.end	= PA_UART_BASE + 0x30,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_UART0,
		.end	= IRQ_UART0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource sdp_uart1_resource[] = {
	[0] = {
		.start 	= PA_UART_BASE + 0x40,
		.end	= PA_UART_BASE + 0x40 + 0x30,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_UART1,
		.end	= IRQ_UART1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource sdp_uart2_resource[] = {
	[0] = {
		.start 	= PA_UART_BASE + 0x80,
		.end	= PA_UART_BASE + 0x80 + 0x30,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_UART2,
		.end	= IRQ_UART2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource sdp_uart3_resource[] = {
	[0] = {
		.start 	= PA_UART_BASE + 0xC0,
		.end	= PA_UART_BASE + 0xC0 + 0x30,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_UART3,
		.end	= IRQ_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

/* EHCI host controller */
static struct resource sdp_ehci0_resource[] = {
        [0] = {
                .start  = PA_EHCI0_BASE,
                .end    = PA_EHCI0_BASE + 0x100,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB_EHCI0,
                .end    = IRQ_USB_EHCI0,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct resource sdp_ehci1_resource[] = {
        [0] = {
                .start  = PA_EHCI1_BASE,
                .end    = PA_EHCI1_BASE + 0x100,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB_EHCI1,
                .end    = IRQ_USB_EHCI1,
                .flags  = IORESOURCE_IRQ,
        },
};

/* USB 2.0 companion OHCI */
static struct resource sdp_ohci0_resource[] = {
        [0] = {
                .start  = PA_OHCI0_BASE,
                .end    = PA_OHCI0_BASE + 0x100,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB_OHCI0,
                .end    = IRQ_USB_OHCI0,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct resource sdp_ohci1_resource[] = {
        [0] = {
                .start  = PA_OHCI1_BASE,
                .end    = PA_OHCI1_BASE + 0x100,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB_OHCI1,
                .end    = IRQ_USB_OHCI1,
                .flags  = IORESOURCE_IRQ,
        },
};

/* xHCI host controller */
static struct resource sdp_xhci0_resource[] = {
        [0] = {
                .start  = PA_XHCI0_BASE,
                .end    = PA_XHCI0_BASE + 0xC700,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB3,
                .end    = IRQ_USB3,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct platform_device sdp_uart0 = {
	.name		= "sdp-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sdp_uart0_resource),
	.resource	= sdp_uart0_resource,
};

static struct platform_device sdp_uart1 = {
	.name		= "sdp-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sdp_uart1_resource),
	.resource	= sdp_uart1_resource,
};

static struct platform_device sdp_uart2 = {
	.name		= "sdp-uart",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(sdp_uart2_resource),
	.resource	= sdp_uart2_resource,
};

static struct platform_device sdp_uart3 = {
	.name		= "sdp-uart",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(sdp_uart3_resource),
	.resource	= sdp_uart3_resource,
};

/* USB Host controllers */
static u64 sdp_ehci0_dmamask = (u32)0xFFFFFFFFUL;
static u64 sdp_ehci1_dmamask = (u32)0xFFFFFFFFUL;
static u64 sdp_ohci0_dmamask = (u32)0xFFFFFFFFUL;
static u64 sdp_ohci1_dmamask = (u32)0xFFFFFFFFUL;
static u64 sdp_xhci0_dmamask = (u32)0xFFFFFFFFUL;

static struct platform_device sdp_ehci0 = {
        .name           = "sdp-ehci",
        .id             = 0,
        .dev = {
                .dma_mask               = &sdp_ehci0_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
        },
        .num_resources  = ARRAY_SIZE(sdp_ehci0_resource),
        .resource       = sdp_ehci0_resource,
};

static struct platform_device sdp_ehci1 = {
        .name           = "sdp-ehci",
        .id             = 1,
        .dev = {
                .dma_mask               = &sdp_ehci1_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
        },
        .num_resources  = ARRAY_SIZE(sdp_ehci1_resource),
        .resource       = sdp_ehci1_resource,
};

static struct platform_device sdp_ohci0 = {
        .name           = "sdp-ohci",
        .id             = 0,
        .dev = {
                .dma_mask               = &sdp_ohci0_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
        },
        .num_resources  = ARRAY_SIZE(sdp_ohci0_resource),
        .resource       = sdp_ohci0_resource,
};

static struct platform_device sdp_ohci1 = {
        .name           = "sdp-ohci",
        .id             = 1,
        .dev = {
                .dma_mask               = &sdp_ohci1_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
        },
        .num_resources  = ARRAY_SIZE(sdp_ohci1_resource),
        .resource       = sdp_ohci1_resource,
};

static struct platform_device sdp_xhci0 = {
        .name           = "xhci-hcd",
        .id             = 0,
        .dev = {
                .dma_mask               = &sdp_xhci0_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
        },
        .num_resources  = ARRAY_SIZE(sdp_xhci0_resource),
        .resource       = sdp_xhci0_resource,
};

static struct resource sdp_unzip_resource[] = {
	[0] = {
		.start 	= PA_UNZIP_BASE,
		.end	= PA_UNZIP_BASE + 0x200,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_GZIP,
		.end	= IRQ_GZIP,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 sdp_unzip_dmamask = (u32)0xFFFFFFFFUL;

static struct platform_device sdp_unzip = {
	.name		= "sdp-unzip",
	.id		= 0,
	.dev = {
		.dma_mask               = &sdp_unzip_dmamask,
		.coherent_dma_mask      = 0xFFFFFFFFUL,
	},
	.num_resources	= ARRAY_SIZE(sdp_unzip_resource),
	.resource	= sdp_unzip_resource,
};

/* USB device controller */
static int sdp1202_otg_lowlevel_init(void)
{
	u32 val;
	void *pad_regs = (void*)VA_PADCTRL_BASE;

	/* SWRESET */
	sdp_set_clockgating(0x10090958, 0x1, 0x0);
	sdp_set_clockgating(0x10090954, 0x7, 0x0);

	/* GPR: XXX per-board settings */
	writel(0x6330dc95, pad_regs + 0x6c);	/* tune */
	
	/* device mode, commonon set*/
	val = readl(pad_regs + 0x74);
	val &= ~(0x20000000);
	val |= 0x1000;
	writel(val, pad_regs + 0x74);

	/* TXPREEMPAMPTUNE0 */	
	val = readl(pad_regs + 0x78);
	val |= 0x5;
	writel(val, pad_regs + 0x78);
	
	/* tune */
	writel(0x6330dc95, pad_regs + 0x6c);
	
	/* SWRESET off */
	sdp_set_clockgating(0x10090958, 0x1, 0x1);
	sdp_set_clockgating(0x10090954, 0x7, 0x2);

	udelay(10);
}

static int sdp1202_otg_phy_init(void)
{
	u32 val;
	void *pad_regs = (void*)VA_PADCTRL_BASE;
	int timeout = 200;

	sdp_set_clockgating(0x10090954, 0x4, 0x4);
	
	while(timeout--) {
		val = readl(pad_regs + 0x84);
		if (!(val & 1))
			break;
		udelay(10);
	}
	if (timeout <= 0) {
		pr_crit("%s: swreset release timeout!\n", __func__);
		return -ETIMEDOUT;
	} else
		return 0;
}

static int sdp1202_otg_phy_exit(void)
{
	sdp_set_clockgating(0x10090954, 0x4, 0x0);
	udelay(50);
	return 0;
}

static struct resource sdp_otg_resource[] = {
        [0] = {
                .start  = PA_OTG_BASE,
                .end    = PA_OTG_BASE + 0x10000,//SZ_4K - 1,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_USB_OTG,
                .end    = IRQ_USB_OTG,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct sdp_hsotg_plat sdp_otg_platdata = {
	.dma		= SDP_HSOTG_DMA_DRV,
	.is_osc		= 0,
	.lowlevel_init 	= sdp1202_otg_lowlevel_init,
	.phy_init	= sdp1202_otg_phy_init,
	.phy_exit	= sdp1202_otg_phy_exit,
};

static u64 sdp_otg_dmamask = (u32)0xFFFFFFFFUL;
static struct platform_device sdp_otg = {
        .name           = "sdp-otg",
        .id             = 0,
        .dev = {
                .dma_mask               = &sdp_otg_dmamask,
                .coherent_dma_mask      = 0xFFFFFFFFUL,
                .platform_data = &sdp_otg_platdata,
        },
        .num_resources  = ARRAY_SIZE(sdp_otg_resource),
        .resource       = sdp_otg_resource,
};

// add sdpGmac platform device by tukho.kim 20091205
#include <plat/sdp_gmac_reg.h>

/*
	fox ap pad ctrl
	0x10090CC4[0] : RGMII Sel
	0x10090CC8[0] : SRAM Sel
*/
static int sdp1202_gmac_init(void)
{
	/* SRAM select*/
	writel(readl((void *)DIFF_IO_BASE0+0x10090CD4) | 0x1, (void *)DIFF_IO_BASE0+0x10090CD4);
	return  0;
}

static struct sdp_gmac_plat sdp_gmac_platdata =  {
	.init = sdp1202_gmac_init,/* board specipic init */
	.phy_mask = 0x0,/*bit filed 1 is masked */
	.napi_weight = 128,/* netif_napi_add() weight value. 1 ~ max */
	.bus_width = 64,
};

// sdpGmac resource 
static struct resource sdpGmac_resource[] = {
        [0] = {
                .start  = PA_GMAC_BASE + SDP_GMAC_BASE,
                .end    = PA_GMAC_BASE + SDP_GMAC_BASE + sizeof(SDP_GMAC_T),
                .flags  = IORESOURCE_MEM,
        },

        [1] = {
                .start  = PA_GMAC_BASE + SDP_GMAC_MMC_BASE,
                .end    = PA_GMAC_BASE + SDP_GMAC_MMC_BASE + sizeof(SDP_GMAC_MMC_T),
                .flags  = IORESOURCE_MEM,
        },

        [2] = {
                .start  = PA_GMAC_BASE + SDP_GMAC_TIME_STAMP_BASE,
                .end    = PA_GMAC_BASE + SDP_GMAC_TIME_STAMP_BASE + sizeof(SDP_GMAC_TIME_STAMP_T),
                .flags  = IORESOURCE_MEM,
        },

        [3] = {
                .start  = PA_GMAC_BASE + SDP_GMAC_MAC_2ND_BLOCK_BASE,
                .end    = PA_GMAC_BASE + SDP_GMAC_MAC_2ND_BLOCK_BASE
                                + sizeof(SDP_GMAC_MAC_2ND_BLOCK_T),  // 128KByte
                .flags  = IORESOURCE_MEM,
        },

        [4] = {
                .start  = PA_GMAC_BASE + SDP_GMAC_DMA_BASE,
                .end    = PA_GMAC_BASE + SDP_GMAC_DMA_BASE + sizeof(SDP_GMAC_DMA_T),  // 128KByte
                .flags  = IORESOURCE_MEM,
        },

        [5] = {
                .start  = IRQ_EMAC,
                .end    = IRQ_EMAC,
                .flags  = IORESOURCE_IRQ,
        },
};

static u64 sdp_mac_dmamask = DMA_BIT_MASK(32);

static struct platform_device sdpGmac_devs = {
        .name           = ETHER_NAME,
        .id             = 0,
        .dev		= {
			.dma_mask = &sdp_mac_dmamask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &sdp_gmac_platdata,
		},
        .num_resources  = ARRAY_SIZE(sdpGmac_resource),
        .resource       = sdpGmac_resource,
};
// add sdpGmac platform device by tukho.kim 20091205 end
//
// mmc

/*
	driving: 0x1009_092C[31:16]
	sample:  0x1009_092C[15:00]
*/
static int sdp1202_mmch_init(SDP_MMCH_T *p_sdp_mmch)
{
	int revision;
	if(p_sdp_mmch->pm_is_valid_clk_delay == false)
	{
		/* save clk delay */
		p_sdp_mmch->pm_clk_delay[0] = readl((void *)VA_SFR0_BASE+0x0009092C);
		p_sdp_mmch->pm_is_valid_clk_delay = true;
	}
	else
	{
		/* restore clk delay */
		writel(p_sdp_mmch->pm_clk_delay[0], (void *)VA_SFR0_BASE+0x0009092C);
	}

	/* sdp1202 evt0 fixup. using pio mode! */
	revision = sdp_get_revision_id();
	if(revision == 0) {
		struct sdp_mmch_plat *platdata;
		platdata = dev_get_platdata(&p_sdp_mmch->pdev->dev);
		platdata->force_pio_mode = true;
	}
	return 0;
}

static struct sdp_mmch_plat sdp_mmc_platdata = {
	.processor_clk = 100000000,
	.min_clk = 400000,
	.max_clk = 50000000,
	.caps =  (MMC_CAP_8_BIT_DATA | MMC_CAP_MMC_HIGHSPEED \
			| MMC_CAP_NONREMOVABLE | MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50),
	.init = sdp1202_mmch_init,
	.fifo_depth = 128,
};

static struct resource sdp_mmc_resource[] = {
        [0] = {
                .start  = PA_MMC_BASE,
                .end    = PA_MMC_BASE + 0x800 -1,
                .flags  = IORESOURCE_MEM,
        },
        [1] = {
                .start  = IRQ_MMCIF,
                .end    = IRQ_MMCIF,
                .flags  = IORESOURCE_IRQ,
        },
};

static u64 sdp_mmc_dmamask = DMA_BIT_MASK(32);

static struct platform_device sdp_mmc = {
	.name		= "sdp-mmc",
	.id		= 0,
	.dev		= {
		.dma_mask = &sdp_mmc_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &sdp_mmc_platdata,
	},
	.num_resources	= ARRAY_SIZE(sdp_mmc_resource),
	.resource	= sdp_mmc_resource,
};


/* SPI master controller */

#define REG_MSPI_TX_CTRL		0x10090268// set 0x1000 for sel SPI
#define MSPI_TX_CTRL_SELSPI		(0x01 << 12)
#define REG_PAD_CTRL_17			VA_PADCTRL_BASE+0x44/* Pad select for SPI */
#define PAD_CTRL_17_SPI_SEL		(0x1<<0)
static int sdp1202_spi_init(void)
{
	int tmp;
	//MSPI Reg TX_CTRL sel_spi
	tmp = readl((void *) (REG_MSPI_TX_CTRL + DIFF_IO_BASE0));
	tmp |= MSPI_TX_CTRL_SELSPI;
	writel(tmp, (void *) (REG_MSPI_TX_CTRL + DIFF_IO_BASE0));

#if 0/* comment for UD board */
	//Pad control Main/Sub Function Selection REGISTER  bit 0 set SPI use SPI_SEL
	tmp = readl(REG_PAD_CTRL_17);
	tmp |= PAD_CTRL_17_SPI_SEL;
	writel(tmp, REG_PAD_CTRL_17);
#endif
	return 0;
}

static struct resource sdp_spi_resource[] = 
{
	[0] = {
			.start = PA_SPI_BASE,
			.end = PA_SPI_BASE + 0x18 - 1,
			.flags = IORESOURCE_MEM,
		},
	[1] = {
			.start = IRQ_SPI,
			.end = IRQ_SPI,
			.flags = IORESOURCE_IRQ,
		},
};

/* SPI Master Controller drain.lee */
static struct sdp_spi_info sdp_spi_master_data = {
	.num_chipselect = CONFIG_SDP_SPI_NUM_OF_CS,
	.init = sdp1202_spi_init,
};

static struct platform_device sdp_spi = {
	.name		= "sdp-spi",
	.id		= 0,
	.dev		= {
		.platform_data = &sdp_spi_master_data,
	},
	.num_resources	= ARRAY_SIZE(sdp_spi_resource),
	.resource	= sdp_spi_resource,
};

/* thermal management */
static struct resource sdp_tmu_resource[] = {
	[0] = {		/* tsc register */
		.start	= PA_TSC_BASE,
		.end	= PA_TSC_BASE + 0x20,
		.flags	= IORESOURCE_MEM,
	},
};

static struct sdp_platform_tmu sdp_tmu_data = {
	.ts = {
		.start_zero_throttle = 40,
		.stop_zero_throttle  = 45,
		.stop_1st_throttle   = 102,
		.start_1st_throttle  = 110,
		.stop_2nd_throttle   = 112,
		.start_2nd_throttle  = 118,
		.stop_3rd_throttle   = 120,
		.start_3rd_throttle  = 125,
	},
	.cpufreq = {
		.limit_1st_throttle = 1000000,
		.limit_2nd_throttle = 600000,
		.limit_3rd_throttle	= 100000,
	},
	.gpufreq = {
		.limit_1st_throttle = 200000,
		.limit_2nd_throttle = 200000,
		.limit_3rd_throttle = 200000,
	},
};

static u64 sdp_tmu_dmamask = 0xffffffffUL;
static struct platform_device sdp_tmu = {
	.name	= "sdp-tmu",
	.id		= 0,
	.dev = {
		.dma_mask = &sdp_tmu_dmamask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = &sdp_tmu_data,
	},
	.num_resources	= ARRAY_SIZE (sdp_tmu_resource),
	.resource	= sdp_tmu_resource,
};

/* Regulator */
/* tps54921 */
static struct regulator_consumer_supply tps54921_supply = {
		.supply = "VDD_ARM",
		.dev_name = NULL,
};

static struct regulator_init_data tps54921_data = {
	.constraints	= {
		.name		= "VDD_ARM range",
		.min_uV		= 720000,
		.max_uV		= 1480000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &tps54921_supply,
};

static struct tps54921_platform_data tps54921_regulator = {
		.i2c_addr	= 0x6E,
#ifdef CONFIG_ARCH_SDP1202_EVAL			
		.i2c_port	= 3,
#else
		.i2c_port	= 2,
#endif
		.init_data	= &tps54921_data,
};

static struct platform_device sdp_pmic_tps54921 = {
	.name	= "sdp_tps54921",
	.id		= 0,
	.dev	= {
		.platform_data = &tps54921_regulator,
	},
};

/* sn1202033 */
static struct regulator_consumer_supply sn1202033_supply[] = {
	{
#ifdef CONFIG_ARCH_SDP1202_EVAL		
		.supply = "VDD_GPU",
#else
		.supply = "VDD_MP0",
#endif
		.dev_name = NULL,
	},
	{
#ifdef CONFIG_ARCH_SDP1202_EVAL		
		.supply = "VDD_MP0",
#else
		.supply = "VDD_GPU",
#endif
		.dev_name = NULL,
	},
	/* for dual MP board */
	{
		.supply = "VDD_MP1",
		.dev_name = NULL,
	},

};

static struct regulator_init_data sn1202033_data[] = {
	[0] = {
		.constraints	= {
#ifdef CONFIG_ARCH_SDP1202_EVAL			
			.name		= "VDD_GPU range",
#else
			.name		= "VDD_MP0 range",
#endif
			.min_uV		= 680000,
			.max_uV		= 1950000,
			.always_on	= 1,
			.boot_on	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = 1,
		.consumer_supplies = &sn1202033_supply[0],
	},
	[1] = {
		.constraints	= {
#ifdef CONFIG_ARCH_SDP1202_EVAL			
			.name		= "VDD_MP0 range",
#else
			.name		= "VDD_GPU range",
#endif
			.min_uV		= 680000,
			.max_uV		= 1950000,
			.always_on	= 1,
			.boot_on	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = 1,
		.consumer_supplies = &sn1202033_supply[1],
	},
	/* for dual MP board */
	[2] = {
		.constraints	= {
			.name		= "VDD_MP1 range",
			.min_uV		= 680000,
			.max_uV		= 1950000,
			.always_on	= 1,
			.boot_on	= 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = 1,
		.consumer_supplies = &sn1202033_supply[2],
	},
};

static struct sn1202033_platform_data sn1202033_regulator[] = {
	[0] = {
		.i2c_addr	= 0xC0,
#ifdef CONFIG_ARCH_SDP1202_EVAL			
		.i2c_port	= 4,
#else
		.i2c_port	= 3,
#endif
		.def_volt	= 1100000,
		.vout_port	= 1,		
		.init_data	= &sn1202033_data[0],
	},
	[1] = {
		.i2c_addr	= 0xC0,
#ifdef CONFIG_ARCH_SDP1202_EVAL			
		.i2c_port	= 4,
#else
		.i2c_port	= 3,
#endif
		.def_volt	= 1100000,
		.vout_port	= 2,		
		.init_data	= &sn1202033_data[1],
	},
	/* for dual MP board */
	[2] = {
		.i2c_addr	= 0xC4,
		.i2c_port	= 3,
		.def_volt	= 1100000,
		.vout_port	= 1,		
		.init_data	= &sn1202033_data[2],
	},
};

static struct platform_device sdp_pmic_sn1202033[] = {
	[0] = {
		.name	= "sdp_sn1202033",
		.id		= 0,
		.dev	= {
			.platform_data = &sn1202033_regulator[0],
		},
	},
	[1] = {
		.name	= "sdp_sn1202033",
		.id		= 1,
		.dev	= {
			.platform_data = &sn1202033_regulator[1],
		},
	},
	/* for dual MP borad */
	[2] = {
		.name	= "sdp_sn1202033",
		.id		= 2,
		.dev	= {
			.platform_data = &sn1202033_regulator[2],
		},
	},
};
/* end Regulator */
static struct resource pmu_resources[] = {
	[0] = {
		.start = IRQ_PMU_CPU0,
		.end = IRQ_PMU_CPU0,
		.flags = IORESOURCE_IRQ,
	},
	[1] = {
		.start = IRQ_PMU_CPU1,
		.end = IRQ_PMU_CPU1,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.start = IRQ_PMU_CPU2,
		.end = IRQ_PMU_CPU2,
		.flags = IORESOURCE_IRQ,
	},
	[3] = {
		.start = IRQ_PMU_CPU3,
		.end = IRQ_PMU_CPU3,
		.flags = IORESOURCE_IRQ,
	},
};


static struct platform_device pmu_device = {
	.name = "arm-pmu",
	.id = 0,
	.num_resources = ARRAY_SIZE(pmu_resources),
	.resource = pmu_resources,
};

static struct platform_device* sdp1202_init_devs[] __initdata = {
	&sdp_uart0,
	&sdp_uart1,
	&sdp_uart2,
	&sdp_uart3,
	&sdp_otg,	
	&sdp_ehci0,
	&sdp_ohci0,
	&sdp_ehci1,
	&sdp_ohci1,
	&sdp_xhci0,
// add sdpGmac platform device by tukho.kim 20091205
	&sdpGmac_devs,	
	&sdp_unzip,
	&sdp_mmc,
	&sdp_spi,
	&sdp_tmu,
	&sdp_pmic_tps54921,
	&sdp_pmic_sn1202033[0],
	&sdp_pmic_sn1202033[1],
	&pmu_device,
};



/* amba devices */
#include <linux/amba/bus.h>
/* for dma330 */
#include <plat/sdp_dma330.h>
static u64 sdp_dma330_dmamask = 0xffffffffUL;


#ifdef CONFIG_SDP_CLOCK_GATING

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);

static int sdp1202_dma330_clk_used_cnt = 0;

/*SDP1202, DMA330_0 and DMA330_1 use same clk source. */
static int sdp1202_dma330_gate_clock(void) {
	/*
		0x10090954[10]		//S/W Reset DMA330
		0x10090944[7]=0		//DMA330 clk Disable of dma module
		0x10090944[6]=0		//DMA330 p-clk Disable of dma module
	*/
	sdp_set_clockgating(0x10090954, 1<<10, 0);
	sdp_set_clockgating(0x10090944, 1<<7, 0);
	sdp_set_clockgating(0x10090944, 1<<6, 0);
	return 0;
}

static int sdp1202_dma330_ungate_clock(void) {
	/*
		0x10090944[6]=0		//DMA330 p-clk Enable of dma module
		0x10090944[7]=0		//DMA330 clk Enable of dma module
		0x10090954[10]		//S/W Reset DMA330
	*/
	sdp_set_clockgating(0x10090944, 1<<6, 1<<6);
	sdp_set_clockgating(0x10090944, 1<<7, 1<<7);
	sdp_set_clockgating(0x10090954, 1<<10, 1<<10);
	return 0;
}
#endif/* CONFIG_SDP_CLOCK_GATING */


static int sdp1202_dma330_0_init(void) {
	/* not to do */
	return 0;
}

static struct dma_pl330_peri sdp_ams_dma330_peri_0[] = {
	[0] = {
		.peri_id = 0,
		.rqtype = MEMTOMEM,
	},
	[1] = {
		.peri_id = 1,
		.rqtype = MEMTOMEM,
	},
};

static struct sdp_dma330_platdata sdp_ams_dma330_0_plat = {
	.nr_valid_peri = ARRAY_SIZE(sdp_ams_dma330_peri_0),
	/* Array of valid peripherals */
	.peri = sdp_ams_dma330_peri_0,
	/* Bytes to allocate for MC buffer */
	.mcbuf_sz = 1024*128,
	.plat_init = sdp1202_dma330_0_init,

#ifdef CONFIG_SDP_CLOCK_GATING
	.plat_clk_gate = sdp1202_dma330_gate_clock,
	.plat_clk_ungate = sdp1202_dma330_ungate_clock,
	.plat_clk_used_cnt = &sdp1202_dma330_clk_used_cnt,
#endif
};

static struct amba_device amba_ams_dma330_0 = {
	.dev		= {
		.init_name			= "sdp-amsdma330_0",
		.platform_data		= &sdp_ams_dma330_0_plat,
		.dma_mask			= &sdp_dma330_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.res		= {
		.start	= PA_AMS_DMA330_0_BASE,
		.end	= PA_AMS_DMA330_0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_DMA330, },
	.periphid	= 0x00241330,/*rev=02, designer=41, part=330*/
};




static int sdp1202_dma330_1_init(void) {
	/* not to do */
	return 0;
}

static struct dma_pl330_peri sdp_ams_dma330_peri_1[] = {
	[0] = {
		.peri_id = 0,
		.rqtype = MEMTOMEM,
	},
	[1] = {
		.peri_id = 1,
		.rqtype = MEMTOMEM,
	},
};

static struct sdp_dma330_platdata sdp_ams_dma330_1_plat = {
	.nr_valid_peri = ARRAY_SIZE(sdp_ams_dma330_peri_1),
	/* Array of valid peripherals */
	.peri = sdp_ams_dma330_peri_1,
	/* Bytes to allocate for MC buffer */
	.mcbuf_sz = 1024*128,
	.plat_init = sdp1202_dma330_1_init,

#ifdef CONFIG_SDP_CLOCK_GATING
	.plat_clk_gate = sdp1202_dma330_gate_clock,
	.plat_clk_ungate = sdp1202_dma330_ungate_clock,
	.plat_clk_used_cnt = &sdp1202_dma330_clk_used_cnt,
#endif
};

static struct amba_device amba_ams_dma330_1 = {
	.dev		= {
		.init_name			= "sdp-amsdma330_1",
		.platform_data		= &sdp_ams_dma330_1_plat,
		.dma_mask			= &sdp_dma330_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.res		= {
		.start	= PA_AMS_DMA330_1_BASE,
		.end	= PA_AMS_DMA330_1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_DMA330, },
	.periphid	= 0x00241330,/*rev=02, designer=41, part=330*/
};
/* end dma330 */

static __initdata struct amba_device *amba_devs[] = {
	&amba_ams_dma330_0,
	&amba_ams_dma330_1,
};

/* end amba devices */

static struct proc_dir_entry *sdp_kernel;

void __init sdp1202_iomap_init (void)
{
	iotable_init(sdp1202_io_desc, ARRAY_SIZE(sdp1202_io_desc));
}

int sdp1202_get_revision_id(void)
{
	static int bInit = 0;
	void __iomem *base = (void __iomem*) (0x10080000 + DIFF_IO_BASE0);
	unsigned int rev_data;
	int rev_id;

	if(bInit)
		return (int) sdp_revision_id;

	writel(0x1F, base + 0x4);	
	while(readl(base) != 0);
	while(readl(base) != 0);
	readl(base + 0x8);
	rev_data = readl(base + 0x8);
	rev_data = (rev_data & 0xC00) >> 10;
	printk("SDP Get Revision ID : %X ", rev_data);

	rev_id = (int) rev_data;

	sdp_revision_id = (u32) rev_id;

	printk("version ES%d\n", sdp_revision_id);
	
	return (int) sdp_revision_id;
}

int sdp_get_revision_id(void)
{
	return (int) sdp_revision_id;
}

static bool sdp1202_is_dual_mp(void)
{
	unsigned int val = readl((void *)VA_EBUS_BASE);

	if (val & ( 1UL << 31)) {
		printk("dual MP board\n");
		sdp_is_dual_mp = true;
	}
	else {
		printk("single MP board\n");
		sdp_is_dual_mp = false;
	}

	return sdp_is_dual_mp;
}

extern unsigned int sdp_sys_mem0_size;
extern unsigned int sdp_sys_mem1_size;
extern unsigned int sdp_sys_mem2_size;

static int proc_read_sdpkernel(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len;
	len = sprintf(page, "%d %d %d\n", sdp_sys_mem0_size >> 20, sdp_sys_mem1_size >> 20, sdp_sys_mem2_size >> 20);
	return len;
}

/* SDP1202 PLL information
 * no		logic	fin 	name	freq	devices
 * ----------------------------------------------------
 * PLL0		2553	24  	ARM 	1350	ARM
 * PLL1		2553	24  	AMS 	1000	SPI IRR SCI MMC
 * PLL2		2553	24  	GPU 	1040	GPU
 * PLL3		2553	24  	DSP 	1200	DMA AHB DSP GZIP 2DGA GP
 * PLL4		2650	74.25	LVDS	148.5	AE2 GPU3 HDMI MMCIF10
 * PLL5		2650	24  	DDR 	1600	DDR-Phy eBUS BUS XMIF
 */
#define SDP1202_FIN	(24000000)

struct sdp1202_pmu_regs {
	u32	pll0_pms;
	u32	pll1_pms;
	u32	pll2_pms;
	u32	pll3_pms;

	u32	pll4_pms;
	u32	pll5_pms;
	u32	reserved0;
	u32	reserved1;

	u32	reserved2;
	u32	reserved3;
	u32	reserved4;
	u32	reserved5;

	u32	pll4_k;
	u32	pll5_k;
	/* ... */
};

static unsigned long sdp1202_recalc_fclk(unsigned long parent_rate)
{
	struct sdp1202_pmu_regs __iomem *regs = (void*)VA_PMU_BASE;
	u32 arm_rate = pll2553_calc_freq(SDP1202_FIN, readl(&regs->pll0_pms));
	return (unsigned long)arm_rate;
}

static unsigned long sdp1202_recalc_armperi(unsigned long parent_rate)
{
	return parent_rate / 4;
}

#if !defined(MHZ)
#define MHZ	1000000
#endif
int __init sdp1202_init_clocks(void)
{
	struct sdp1202_pmu_regs __iomem *regs = (void*)VA_PMU_BASE;
	struct clk *fclk, *pclk, *busclk;
	struct clk *armperi_clk;
	u32 arm_rate, ddr_rate, dsp_rate;
	struct device *cpu_dev = get_cpu_device(0);

	arm_rate = pll2553_calc_freq(SDP1202_FIN, readl(&regs->pll0_pms));
	ddr_rate = pll2650_calc_freq(SDP1202_FIN, readl(&regs->pll5_pms), readl(&regs->pll5_k));
	dsp_rate = pll2553_calc_freq(SDP1202_FIN, readl(&regs->pll3_pms));

	printk (KERN_INFO "SDP1202 PLLs: arm=%d.%03d, ddr=%d.%03d, dsp=%d.%03d Mhz\n",
			arm_rate / MHZ, arm_rate % HZ / 1000,
			ddr_rate / MHZ, ddr_rate % HZ / 1000,
			dsp_rate / MHZ, dsp_rate % HZ / 1000);

	fclk = clk_register_sdp_scalable_clk(cpu_dev, "fclk", NULL, sdp1202_recalc_fclk);
	clk_register_fixed_rate(NULL, "ddrpllclk", NULL, CLK_IS_ROOT, ddr_rate);
	clk_register_fixed_rate(NULL, "dsppllclk", NULL, CLK_IS_ROOT, dsp_rate);

	busclk = clk_register_fixed_rate(NULL, "busclk", "ddrpllclk", 0, ddr_rate / 4);
	pclk = clk_register_fixed_rate(NULL, "pclk", "dsppllclk", 0, dsp_rate / 6);
	armperi_clk = clk_register_sdp_scalable_clk(NULL, "arm_peri", "fclk", sdp1202_recalc_armperi);
	
	/* TODO: use static lookup table? */
	clk_register_clkdev(fclk, "fclk", NULL);
	clk_register_clkdev(armperi_clk, NULL, "smp_twd");
	clk_register_clkdev(pclk, "sdp_uart", NULL);
	clk_register_clkdev(pclk, "sdp_timer", NULL);
	clk_register_clkdev(pclk, NULL, "sdp_i2c");
	clk_register_clkdev(pclk, "sdp_spi", NULL);
	clk_register_clkdev(busclk, NULL, "sdp-dma330.0");

	return 0;
}

void __init sdp1202_init(void)
{
	int i;
	u32 val;

	sdp1202_get_revision_id();
	val = readl((void *) VA_PMU_BASE + 0x154) & 0x10;	//get usb 3.0 reset
	if(!val)	//if reset on
	{
		sdp_xhci0_resource[0].flags = 0;	//remove IO_RESOURCE_MEM flag
	}

	/* pmic default voltage */
	if (sdp_revision_id == 0) { /* ES0 */
		tps54921_regulator.def_volt = 1170000;
	} else { /* ES1 */
		tps54921_regulator.def_volt = 1130000;
	}
	
	platform_add_devices(sdp1202_init_devs, ARRAY_SIZE(sdp1202_init_devs));

	/* for dual MP board */
	if (sdp1202_is_dual_mp())
		platform_device_register(&sdp_pmic_sn1202033[2]);

	/* amba devices register */
	printk("AMBA : amba devices registers..");
	for (i = 0; i < (int) ARRAY_SIZE(amba_devs); i++)
	{
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
	
	sdp_kernel = create_proc_entry("sdp_kernel", 0644, NULL);
	if(sdp_kernel == NULL)
	{
		printk(KERN_ERR "[sdp_kernel] fail to create proc sdp_kernel info\n");
	}
	else
	{
		sdp_kernel->read_proc = proc_read_sdpkernel;
		printk("/proc/sdp_version is registered!\n");
	}
}

int get_sdp_board_type(void)
{
	return -1;
}
EXPORT_SYMBOL(get_sdp_board_type);
