/*
 *  linux/arch/arm/mach-mb8ac0300/ebus_ind.h
 *
 *  Copyright (C) 2012 FUJITSU SEMICONDUCTOR LIMITED
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MACH_EBUS_IND_H
#define __MACH_EBUS_IND_H

/******************************************************************************
 * Define
 *****************************************************************************/
/* Polling Time (usec) */
#define EBUS_TIME_INIT			100
#define EBUS_TIME_MEM			0
#define EBUS_TIME_CONFIG		0
#define EBUS_TIME_DMAC			0

/* Address/Offset Alignment */
#define EBUS_ALIGN_MASK			(~(0x3UL << 0))

/* DMAC Transfert Direction */
#define EBUS_DMAC_WRITE			1
#define EBUS_DMAC_READ			0

/* Error number base */
#define EBUS_ERR_BASE			10000

/******************************************************************************
 * Register (Memory Write/Read, Config Write/Read)
 *****************************************************************************/
/* Memory TLP Header Register(688h) */
/* bit15-12 0:Function Number */
/* bit11-8  0:TC Number */
#define EBUS_MTLPHEAD			((0x0 << 12) \
				       | (0x0 << 8))

/* Memory TLP Transfer Instruction(694h) */
/* bit0 1:Memory Write TLP */
#define EBUS_MTLPINST_MTLPINSTW		(0x1 << 0)
/* bit0 0:Memory Read TLP */
#define EBUS_MTLPINST_MTLPINSTR		(0x0 << 0)

/* Memory TLP Transfer Status Register(6A0h) */
/* bit6 1:Write Transfer Busy */
#define EBUS_MTLPSTS_WTRSBUSY_MASK	(0x1 << 6)

/* Memory TLP Transfer Interrupt Status Register (698h) */
/* bit0 1:Memory Write Transaction Normal End */
#define EBUS_MTLPINTSTS_MWNML_MASK	(0x1 << 0)

/* Memory TLP Transfer Status Register(6A0h) */
/* bit5 1:Read Transfer Busy */
#define EBUS_MTLPSTS_RTRSBUSY_MASK	(0x1 << 5)

/* Memory TLP Transfer Status Register(6A0h) */
/* bit2-0  b'000：Successful Completion(SC) */
/*         b'001：Completer Abort(CA) */
/*         b'010：Unsupported Request(UR) */
/*         b'100：Completion Timeout */
#define EBUS_MTLPSTS_COMPSTS_MASK	(0x7 << 0)

/* Destination Configuration ID(600h) */
/* bit15-8 0:Bus Number */
/* bit7-3  0:Device Number */
/* bit2-0  0:Function Number */
#define EBUS_DESTCONFIGID		((0x0 << 8) \
				       | (0x0 << 3) \
				       | (0x0 << 0))

/* Config/IO Transfer Status(618h) */
/* bit5 1:Config,I/O transfer busy */
#define EBUS_CONFIGIO_IO_TRSBUSY_MASK	(0x1 << 5)
/* bit2-0 b'000：Successful Completion(SC) */
/*        b'001：Completer Abort(CA) */
/*        b'010：Unsupported Request(UR) */
/*        b'100：Completion Timeout */
/*        b'111：Configuration Request Retry Status(CRS) */
#define EBUS_CONFIGIO_IO_COMPSTS_MASK	(0x7 << 0)

/******************************************************************************
 * Register (DMA)
 *****************************************************************************/
/* Demand DMA Transfer Setting Register for DMA x ch(E10h/E30h) */
/* bit 21-20   1:Single Request */
/* bit 18-16   0:TC Number*/
/* bit 15-13   0:Function Number*/
/* bit 12-0  128:DMA Transfer Split Size */
#define EBUS_DEMANDMASET		((0x0  << 20) \
				       | (0x0  << 16) \
				       | (0x0  << 13) \
				       | (0x80 << 0))
/* DMAC Instruction(C00h) */
/* bit5-4 1:DMA start */
#define EBUS_DMACINST_TRSINSTSTART	(0x1 << 4)
/* bit5-4 2:DMA interrupt clear */
#define EBUS_DMACINST_TRSINSTCLEAR	(0x2 << 4)

/* Demand DMA Transfer Status Register 1 for DMA x ch(E18h/E38h) */
/* bit9 1: DMA transfer busy */
#define EBUS_DEMANDMASTS_STS_MASK	(0x1 << 9)
/* bit7 1:Suspended by user */
/* bit5 1:Suspended by Poisoned TLP */
/* bit4 1:Suspended by Completion Timeout */
/* bit3 1:Suspended by Unsupported Request */
/* bit2 1:Suspended by Completion Abort */
/* bit1 1:Suspended by AXI Slave Error(Demand DMA) */
#define EBUS_DEMANDMASTS_ERR_MASK	((0x1 << 7) \
				       | (0x1 << 5) \
				       | (0x1 << 4) \
				       | (0x1 << 3) \
				       | (0x1 << 2) \
				       | (0x1 << 1))

/******************************************************************************
 * Register (Init)
 *****************************************************************************/
/* Root Complex Register(09Ch) */
/* bit4 1:CRS Soft Ware Visibility Enable */
/* bit3 1:PME Interrupt Enable */
/* bit2 1:System Error on Fatal Error Enable */
/* bit1 1:System Error on Non-Fatal Error Enable */
/* bit0 1:System Error on Correctable Error Enable */
#define EBUS_ROOTCTL				((0x1 << 4) \
					       | (0x1 << 3) \
					       | (0x1 << 2) \
					       | (0x1 << 1) \
					       | (0x1 << 0))

/* Memory TLP Transfer Interrupt Mask Register(69Ch) */
/* bit4 1:Instruction Abort Interrupt Mask */
/* bit3 1:MRd Error End Interrupt Mask */
/* bit2 1:MRd Normal End Interrupt Mask */
/* bit1 1:MWr Error End Interrupt Mask */
/* bit0 1:MWr Normal End Interrupt Mask */
#define EBUS_MTLPINRERR			 	((0x1 << 4) \
					       | (0x1 << 3) \
					       | (0x1 << 2) \
					       | (0x1 << 1) \
					       | (0x1 << 0))

/* Power Control/Status(408h) */
/* bit0：0(PERST#Release) */
#define EBUS_POWERCSR_MASK			(~(0x1UL << 0))

/* Root Complex Register(DMAC) */
/* DMAC Mode Control(C04h) */
/* bit15 0:Demmand DMA(ch=F) */
/* bit14 0:Demmand DMA(ch=E) */
/* bit13 0:Demmand DMA(ch=D) */
/* bit12 0:Demmand DMA(ch=C) */
/* bit11 0:Demmand DMA(ch=B) */
/* bit10 0:Demmand DMA(ch=A) */
/* bit9  0:Demmand DMA(ch=9) */
/* bit8  0:Demmand DMA(ch=8) */
/* bit7  0:Demmand DMA(ch=7) */
/* bit6  0:Demmand DMA(ch=6) */
/* bit5  0:Demmand DMA(ch=5) */
/* bit4  0:Demmand DMA(ch=4) */
/* bit3  0:Demmand DMA(ch=3) */
/* bit2  0:Demmand DMA(ch=2) */
/* bit1  0:Demmand DMA(ch=1) */
/* bit0  0:Demmand DMA(ch=0) */
#define EBUS_DMAMODECTL				((0x0 << 15) \
					       | (0x0 << 14) \
					       | (0x0 << 13) \
					       | (0x0 << 12) \
					       | (0x0 << 11) \
					       | (0x0 << 10) \
					       | (0x0 << 9) \
					       | (0x0 << 8) \
					       | (0x0 << 7) \
					       | (0x0 << 6) \
					       | (0x0 << 5) \
					       | (0x0 << 4) \
					       | (0x0 << 3) \
					       | (0x0 << 2) \
					       | (0x0 << 1) \
					       | (0x0 << 0))

/* DMAC Interrupt Mask(C0Ch) */
/* bit7 1:Transfer Abort by user operation */
/* bit6 1:PCIE Reset Detect */
/* bit5 1:PCIE Poisoned TLP Detect */
/* bit4 1:PCIE Completion Abort Detect */
/* bit3 1:PCIE Unsupported Request Detect */
/* bit2 1:PCIE Completion Timeout Detect */
/* bit1 1:AXI Error Detect */
/* bit0 1:Transfer Complete */
#define EBUS_DMAINTERR_MASK		 	((0x1 << 7) \
					       | (0x1 << 6) \
					       | (0x1 << 5) \
					       | (0x1 << 4) \
					       | (0x1 << 3) \
					       | (0x1 << 2) \
					       | (0x1 << 1) \
					       | (0x1 << 0))

/* Base Address Register1(014h) */
/* Register offset address */
#define EBUS_REG_BASEADDR1			0x014
/* bit31-0 0:Base Address */
#define EBUS_BASEADDR1				0x00000000

/* Power Management Control/Status(044h) */
/* Register offset address */
#define EBUS_REG_PMSTCTL			0x044
/* bit8 1:PME enable */
#define EBUS_PMSTCTL				(0x1 << 8)

/* Command Register(004h) */
/* Register offset address */
#define EBUS_REG_COMMAND			0x004
/* bit8 1:SERR# enable */
/* bit6 1:Master Data Parity Error enable */
/* bit2 1:Memory and I/O Request enable */
/* bit1 1:Memory space access enable */
/* bit0 1:I/O space access enable */
#define EBUS_COMMAND				((0x1 << 8) \
					       | (0x1 << 6) \
					       | (0x1 << 2) \
					       | (0x1 << 1) \
					       | (0x1 << 0))

/******************************************************************************
 * Root Complex Configration Register
 *****************************************************************************/
typedef struct  {				/* 000h - 03Fh */
	unsigned long dev_vend;			/* 000h */
	unsigned long st_com;			/* 004h */
	unsigned long conf_head[14];
} conf_head_t;

typedef struct  {				/* 040h - 04Fh */
	unsigned long pm_next_cap;		/* 040h */
	unsigned long power_manage_ctl;		/* 044h */
	unsigned long reserve[2];
} pm_cap_t;

typedef struct  {				/* 050h - 06Fh */
	unsigned long msi_cap[8];
} msi_cap_t;

typedef struct  {				/* 070h - 07Fh */
	unsigned long msix_cap[4];
} msix_cap_t;

typedef struct  {				/* 080h - 0BCh */
	unsigned long	pciecap_nexp_cap;	/* 080h */
	unsigned long	device_cap;		/* 084h */
	unsigned long	dev_st_ctl;		/* 088h */
	unsigned long	link_cap;		/* 08ch */
	unsigned long	link_st_ctl;		/* 090h */
	unsigned long	slot_cap;		/* 094h */
	unsigned long	slot_st_ctl;		/* 098h */
	unsigned long	root_cap_ctl;		/* 09Ch */
	unsigned long	root_status;		/* 0A0h */
	unsigned long	device_cap2;		/* 0A4h */
	unsigned long	dev_st_ctl2;		/* 0A8h */
	unsigned long	link_cap2;		/* 0ACh */
	unsigned long	link_st_ctl2;		/* 0B0h */
	unsigned long	slot_cap2;		/* 0B4h */
	unsigned long	slot_ctl2;		/* 0B8h */
} pci_cap_t;

typedef struct  {				/* 0BCh - 0FFh */
	unsigned long rc_reserve1[17];
} rc_reserve1_t;

typedef struct  {				/* 100h - 1FFh */
	unsigned long adv_err_rpt_cap[64];
} adv_err_rpt_cap_t;

typedef struct  {				/* 200h - 2FFh */
	unsigned long vir_ch_cap[64];
} vir_ch_cap_t;

typedef struct  {				/* 300h - 30Bh */
	unsigned long dev_serial_num_cap[3];
} dev_serial_num_cap_t;

typedef struct  {				/* 30Ch - 3FFh */
	unsigned long rc_reserve2[61];
} rc_reserve2_t;

typedef struct  {				 /* 400h - 5FFh */
	unsigned long pcie_cap_head;		/* 400h */
	unsigned long vend_sp_head;		/* 400h */
	unsigned long power_csr;		/* 408h */
	unsigned long error_detect_enable;	/* 40ch */
	unsigned long err_st_cnt[4];		/* 410h - 41Fh */
	unsigned long dllm_ctl[4];		/* 420h - 42Fh */
	unsigned long tlm_ctl[12];		/* 430h - 45Fh */
	unsigned long plm_ctl[40];		/* 460h - 4FFh */
	unsigned long reserve[64];		/* 500h - 5FFh */
} app_regi_block_t;

typedef struct  {				 /* 600h - 7FFh */
	unsigned long rsv_destid;		/* 600h */
	unsigned long rsv_confregno;		/* 604h */
	unsigned long dst_io_address;		/* 608h */
	unsigned long byte_enable;		/* 60ch */
	unsigned long trans_write_data;		/* 610h */
	unsigned long trans_read_data;		/* 614h */
	unsigned long confio_trans_status;	/* 618h */
	unsigned long confio_trans_log;		/* 61ch */
	unsigned long reserve1[8];		/* 620h - 62Fh */

	unsigned long conf_t0_write_inst;	/* 640h */
	unsigned long conf_t0_read_inst;	/* 644h */
	unsigned long conf_t1_write_inst;	/* 648h */
	unsigned long conf_t1_read_inst;	/* 64Ch */
	unsigned long io_write_inst;		/* 650h */
	unsigned long io_read_inst;		/* 654h */
	unsigned long confio_intclr_inst;	/* 658h */
	unsigned long reserve2;			/* 65Ch */
	unsigned long reserve3[8];		/* 660h - 67Fh */

	unsigned long dst_mem_low;		/* 680h */
	unsigned long dst_mem_up;		/* 684h */
	unsigned long mem_tlp_head;		/* 688h */
	unsigned long mem_tlp_write_data;	/* 68Ch */
	unsigned long mem_tlp_read_data;	/* 690h */
	unsigned long mem_tlp_inst;		/* 694h */
	unsigned long mem_tlp_interrupt_status;
						/* 698h */
	unsigned long mem_tlp_interrupt_mask;	/* 69Ch */
	unsigned long mem_tlp_trans_status;	/* 6A0h */
	unsigned long reserve4[87];		/* 6A4h - 7FFh */
} config_io_ctl_t;

typedef struct  {				/* 800h - 83Fh*/
	unsigned long pcie_provision[16];
} pcie_provision_t;

typedef struct  {				/* 840h - 87Fh */
	unsigned long pcie_set[16];
} pcie_set_t;

typedef struct  {				/* 880h - 8FhF */
	unsigned long axi_bridge_ctl[32];
} axi_bridge_ctl_t;

typedef struct  {				/* 900h - BFFh */
	unsigned long axi_bridge_st[192];
} axi_bridge_st_t;

typedef struct  {				/* C00h - DFFh*/
	unsigned long dmac_instruction;		/* C00h */
	unsigned long dmac_mode_control;	/* C04h */
	unsigned long dmac_interrupt_status;	/* C08h */
	unsigned long dmac_interrupt_mask;	/* C0Ch */
	unsigned long reserve[124];		/* C10h - DFFh */
} dma_ctl_t;

typedef struct  {
	unsigned long pci_low;			/* E00h */
	unsigned long pci_up;			/* E04h */
	unsigned long axi_addr;			/* E08h */
	unsigned long size;			/* E0Ch */
	unsigned long dma_trans_set;		/* E10h */
	unsigned long reserve1;			/* E14h */
	unsigned long dma_status;		/* E18h */
	unsigned long reserve2;			/* E1Ch */
} dma_ch_t;

typedef struct  {				 /* E00h - FFFh */
	dma_ch_t dma_ch[16];
} dma_trs_t;

typedef struct  {
	conf_head_t		conf_head;	     /* 000 - 03Fh */
	pm_cap_t		pm_cap;		     /* 040 - 04Fh */
	msi_cap_t		msi_cap;	     /* 050 - 06Fh */
	msix_cap_t		msix_cap;	     /* 070 - 07Fh */
	pci_cap_t		pci_cap;	     /* 080 - 0BCh */
	rc_reserve1_t		rc_reserve1;	     /* 0BC - 0FFh */
	adv_err_rpt_cap_t	adv_err_rpt_cap;     /* 100 - 1FFh */
	vir_ch_cap_t		vir_ch_cap;	     /* 200 - 2FFh */
	dev_serial_num_cap_t	dev_serial_num_cap;  /* 300 - 30Bh */
	rc_reserve2_t		rc_reserve2;	     /* 30C - 1FCh */
	app_regi_block_t	app_regi_block;	     /* 400 - 5FFh */
	config_io_ctl_t	config_io_ctl;		     /* 600 - 7FFh */
	pcie_provision_t	cie_provision;	     /* 800 - 83Fh */
	pcie_set_t		pcie_set;	     /* 840 - 87Fh */
	axi_bridge_ctl_t	axi_bridge_ctl;	     /* 880 - 8FhF */
	axi_bridge_st_t		axi_bridge_st;	     /* 900 - BFFh */
	dma_ctl_t		dma_ctl;	     /* C10 - DFFh */
	dma_trs_t		dma_trs;	     /* E00 - FFFh */
} ebus_rc_reg_t;

#endif /* __MACH_EBUS_IND_H */
