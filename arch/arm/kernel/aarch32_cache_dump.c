/*
 * Dump cache buffer(AArch32) for debugging purposes.
 *
 * Created by <js07.lee@samsung.com>
 * Copyright (C) 2016 by Samsung Electronic, Inc.
 *
 * NOTE : All macros in this file are only dedicated to AArch32.
 */

#include <linux/cpu.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cp15.h>
#include <linux/kallsyms.h>

#define BUFF_SIZE	512
#define BUFF2_SIZE	40

#define	PRINT_DUMP(fmt, args ...)    printk(KERN_EMERG fmt, ##args)
#define	INST_CACHE	0x1C
#define	DATA_CACHE	0xDC

/* CSSELR (Cache Size Selection Register) */
#define CSSELR_L1_ICACHE	((0x0 << 1) | 0x1)
#define CSSELR_L1_DCACHE	((0x0 << 1) | 0x0)
#define CSSELR_L2_ICACHE	((0x1 << 1) | 0x1)
#define CSSELR_L2_DCACHE	((0x1 << 1) | 0x0)

/* 31    24 23   21    18 17                   0
 * ---------------------------------------------
 * | RAMID |RES0|   Way  |        Index        |
 * ---------------------------------------------
 */
#define	L1_I_TAG_RAM			0x00
#define	L1_I_DATA_RAM			0x01
#define	L1_I_BTB_RAM			0x02
#define	L1_I_GHB_RAM			0x03
#define	L1_I_TLB_RAM			0x04
#define	L1_I_INDIRECT_PREDIECTOR_RAM	0x05
#define	L1_D_TAG_RAM			0x08
#define	L1_D_DATA_RAM			0x09
#define	L1_D_TLB_RAM			0x0A
#define	L2_TAG_RAM			0x10
#define	L2_DATA_RAM			0x11
#define	L2_SNOOP_TAG_RAM		0x12
#define	L2_DATA_ECC_RAM			0x13
#define	L2_DIRTY_RAM			0x14
#define	L2_TLB_RAM			0x18

/*
 * Read Instruction L1 Data register 0~3
 * - 4.3.62 Instruction L1 Data n Register,EL1
 */
#define Read_I_Data_L1_0_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c0, 0" : "=r"(Rd))
#define Read_I_Data_L1_1_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c0, 1" : "=r"(Rd))
#define Read_I_Data_L1_2_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c0, 2" : "=r"(Rd))
#define Read_I_Data_L1_3_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c0, 3" : "=r"(Rd))

/*
 * Read Data L1 Data register 0~3
 * - 4.3.63 Data L1 Data n Register,EL1
 */
#define Read_D_Data_L1_0_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c1, 0" : "=r"(Rd))
#define Read_D_Data_L1_1_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c1, 1" : "=r"(Rd))
#define Read_D_Data_L1_2_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c1, 2" : "=r"(Rd))
#define Read_D_Data_L1_3_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c1, 3" : "=r"(Rd))
#define Read_D_Data_L1_4_Reg(Rd)	asm volatile("mrc   p15, 0, %0, c15, c1, 4" : "=r"(Rd))

/*
 * Write RAMINDEX register
 * - 4.3.64 RAM Index operation
 */
#define Write_Ramindex_Reg(Ri)		asm volatile("mcr   p15, 0, %0, c15, c4, 0" : : "r"(Ri))

static void L1_enable(void)
{
	unsigned int control_reg;

	asm volatile("mrc   p15, 0, %0, c1, c0, 0 " : "=r"(control_reg));
	control_reg |= CR_C;	/* [2]  : data cache */
	control_reg |= CR_I;	/* [12] : instruction cache */
	asm volatile("mcr   p15, 0, %0, c1, c0, 0 " : : "r"(control_reg));
	isb();
}

static void L1_disable(void)
{
	unsigned int control_reg;

	asm volatile("mrc   p15, 0, %0, c1, c0, 0 " : "=r"(control_reg));
	control_reg &= ~CR_C;	/* [2]  : data cache */
	control_reg &= ~CR_I;	/* [12] : instruction cache */
	asm volatile("mcr   p15, 0, %0, c1, c0, 0 " : : "r"(control_reg));
	isb();
}

void read_ccsidr(unsigned int csselr, unsigned int *n_index,
		 unsigned int *n_way, unsigned int *raw_cache_line)
{
	unsigned int ccsidr = 0xDEADDEAD;

	if (!n_index || !n_way || !raw_cache_line) {
		*n_way = 0;
		*n_index = 0;
		return;
	}

	/* Set CSSELR */
	asm volatile("mcr   p15, 2, %0, c0, c0, 0 " : : "r"(csselr));

	/* Get CCSIDR */
	asm volatile("mrc   p15, 1, %0, c0, c0, 0 " : "=r"(ccsidr));

	/* set */
	*n_index	= ((ccsidr >> 13) & 0x7FFF) + 1;
	/* associativity : 0x1 => 2way, 0x10 => 3way */
	*n_way		= ((ccsidr >>  3) & 0x03FF) + 1;
	*raw_cache_line = (1 << (ccsidr & 0x7)) + 2;
}

static void L1_I_Data_read(unsigned ramindex, unsigned *data0, unsigned *data1)
{
	Write_Ramindex_Reg(ramindex);
	dsb();
	isb();
	Read_I_Data_L1_0_Reg(*data0);
	Read_I_Data_L1_1_Reg(*data1);
}

static void L1_D_Data_read(unsigned ramindex, unsigned *data0, unsigned *data1)
{
	Write_Ramindex_Reg(ramindex);
	dsb();
	isb();
	Read_D_Data_L1_0_Reg(*data0);
	Read_D_Data_L1_1_Reg(*data1);
}

/* retval : Tag address */
unsigned long long get_L1_tag_data(unsigned int cache_type, unsigned way_index,
				   unsigned *nonsecure, unsigned *valid)
{
	unsigned int data0, data1;
	unsigned ramindex;
	unsigned long long ret;

	/* way_index = (way_i << 18) | (index_i << 6); */

	switch (cache_type) {
	case INST_CACHE:
		ramindex = L1_I_TAG_RAM << 24 | way_index;
		L1_I_Data_read(ramindex, &data0, &data1);
		/* ILDATA0  : Physical address tag[43:12] */
		ret = data0 << 12 | (way_index & 0x3FFF);
		*nonsecure = (data1 >>  0) & 0x1;
		*valid     = (data1 >>  1) & 0x1;
		break;

	case DATA_CACHE:
		ramindex = L1_D_TAG_RAM << 24 | way_index;
		L1_D_Data_read(ramindex, &data0, &data1);
		/* DL1DATA0[29:0] Physical address tag[43:14] */
		ret = (data0 & 0x3FFFFFFF) << 14 | (way_index & 0x3FFF);
		*nonsecure = (data0 >> 30) & 0x1;
		*valid     = (data1 >>  0) & 0x3;
		break;

	default:
		return -1;
	}

	return ret;
}

void get_L1_cache_data(char *buf, unsigned int cache_type,
		       unsigned way_index, unsigned dword_i)
{
	unsigned int data0 = 0, data1 = 0;
	unsigned int ramindex;
	char buf2[BUFF2_SIZE];

	/*
	 * RAMINDEX for L1-I Data RAM
	 *
	 * 31    24 23 20 19 18 17   14 13    6        0
	 * ----------------------------------------------
	 * |RAMID  |     | way |       |   VA  |        |
	 * ----------------------------------------------
	 * RAMID = 0x01
	 * way[1:0] : Way select
	 * VA[13:6] : Set select
	 * VA[5:4]  : Bank select
	 * VA[3]    : upper or lower doubleword within the quadword
	 * ILDATA1  : Data word 1
	 * ILDATA0  : Data word 0
	 *
	 * RAMINDEX for L1-D Data RAM
	 *
	 * 31    24 23 20 19 18 17   14 13    6        0
	 * ----------------------------------------------
	 * |RAMID  |        |  |       |   PA  |        |
	 * ----------------------------------------------
	 * RAMID = 0x09
	 * way      : Way select
	 * PA[13:6] : Set select
	 * PA[5:4]  : Bank select
	 * PA[3]    : upper or lower doubleword within the quadword
	 * ILDATA1  : Data word 1
	 * ILDATA0  : Data word 0
	 */
	switch (cache_type) {
	case INST_CACHE:
		ramindex = L1_I_DATA_RAM << 24 |	/*  [31:24] */
			(way_index & 0xC3FC0) |		/* [19:18][13:6] */
			(dword_i << 3);			/* [5:4][3] */
		L1_I_Data_read(ramindex, &data0, &data1);
		break;

	case DATA_CACHE:
		ramindex = L1_D_DATA_RAM << 24 |	/* [31:24] */
			(way_index & 0x43FC0) |		/* [18][13:6] */
			(dword_i << 3);			/* [5:4][3] */
		L1_D_Data_read(ramindex, &data0, &data1);
		break;
	}

	snprintf(buf2, BUFF2_SIZE, "%08x ", data0);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
	snprintf(buf2, BUFF2_SIZE, "%08x ", data1);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
}

void __dump_L1_cache(unsigned int cache_type)
{
	unsigned int way_i, index_i, dword_i;
	unsigned n_way, n_index;
	unsigned way_index;
	unsigned csselr;
	unsigned raw_cache_line, cache_line;
	unsigned long long phys_addr;
	unsigned nonsecure, valid;

	char buf[512];
	char buf2[40];

	if (cache_type == INST_CACHE)
		csselr = CSSELR_L1_ICACHE;
	else if (cache_type == DATA_CACHE)
		csselr = CSSELR_L1_DCACHE;
	else
		return;

	/* Read Cache Size ID Register */
	read_ccsidr(csselr, &n_index, &n_way, &raw_cache_line);
	cache_line = 1 << raw_cache_line;

	PRINT_DUMP("-----------------------------------------------------\n");
	PRINT_DUMP(" Cache Dump : %s\n", cache_type == INST_CACHE ? "IC" : "DC");
	PRINT_DUMP("-----------------------------------------------------\n");
	PRINT_DUMP("|   address  |sec| set  |way |v| 00       04       08       0C       "
		   "10       14       18       1C       20       24       28       "
		   "2C       30       34       38       3C\n");

	/*
	 * ICACHE | n_index : 0xFF, n_way : 2
	 * DCACHE | n_index : 0xFF, n_way : 1
	 */
	for (index_i = 0; index_i < n_index ; index_i++) {
		for (way_i = 0; way_i < n_way ; way_i++) {
			/* doubleword print */
			for (dword_i = 0; dword_i < cache_line / sizeof(int) / 2 ; dword_i++) {
				/*
				 * RAMINDEX for L1-I Tag RAM
				 * 31    24 23 20 19 18 17   14 13    6        0
				 * ----------------------------------------------
				 * |RAMID  |     | way |       |   VA  |        |
				 * ----------------------------------------------
				 * RAMID = 0x00
				 * way[1:0] : Way select
				 * VA[13:7] : Row select
				 * VA[6]    : Bank select
				 * ILDATA0  : Physical address tag[43:12]
				 *
				 * RAMINDEX for L1-D Tag RAM
				 * 31    24 23 20 19 18 17   14 13    6        0
				 * ----------------------------------------------
				 * |RAMID  |        |  |       |   PA  |        |
				 * ----------------------------------------------
				 * RAMID = 0x08
				 * way      : Way select
				 * PA[13:8] : Row select
				 * PA[7:6]  : Bank select
				 * DL1DATA0[29:0] Physical address tag[43:14]
				 */
				way_index = (way_i << 18) | (index_i << 6);

				phys_addr = get_L1_tag_data(cache_type, way_index, &nonsecure, &valid);

				if (!dword_i) {
					buf[0] = '\0';
					snprintf(buf2, BUFF2_SIZE, "|0x%010llx", phys_addr);
					strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
					snprintf(buf2, BUFF2_SIZE, "| %s", nonsecure ? "ns" : " s");
					strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
					snprintf(buf2, BUFF2_SIZE, "|0x%04x", index_i);
					strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
					snprintf(buf2, BUFF2_SIZE, "|0x%02x", way_i);
					strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
					snprintf(buf2, BUFF2_SIZE, "|%s| ", valid ? "v" : "-");
					strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
				}

				get_L1_cache_data(buf, cache_type, way_index, dword_i);
			}
			strncat(buf, "\n", BUFF_SIZE - 1 - strlen(buf));
			PRINT_DUMP("%s", buf);
		}
	}
}

/* retval:
   1 = filled
   0 = NOT filled
   */
int get_tlb_data(char *buf, unsigned ramindex)
{
	unsigned int data0 = 0, data1 = 0;
	unsigned int data2 = 0, data3 = 0;
	char buf2[BUFF2_SIZE];

	Write_Ramindex_Reg(ramindex);
	dsb();
	isb();

	if (ramindex >> 24 == L1_I_TLB_RAM) {
		Read_I_Data_L1_0_Reg(data0);
		Read_I_Data_L1_1_Reg(data1);
		Read_I_Data_L1_2_Reg(data2);
		Read_I_Data_L1_3_Reg(data3);
	} else if (ramindex >> 24 == L1_D_TLB_RAM) {
		Read_D_Data_L1_0_Reg(data0);
		Read_D_Data_L1_1_Reg(data1);
		Read_D_Data_L1_2_Reg(data2);
		Read_D_Data_L1_3_Reg(data3);
	} else
		return 0;

	snprintf(buf2, BUFF2_SIZE, "|%04x|", ramindex & 0x3F);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));

	snprintf(buf2, BUFF2_SIZE, "%08x ", data0);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
	snprintf(buf2, BUFF2_SIZE, "%08x ", data1);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
	snprintf(buf2, BUFF2_SIZE, "%08x ", data2);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));
	snprintf(buf2, BUFF2_SIZE, "%08x ", data3);
	strncat(buf, buf2, BUFF_SIZE - 1 - strlen(buf));

	return 1;
}

void __dump_tlb_cache(void)
{
	unsigned int entry_i, ramindex;
	int filled;

	char buf[BUFF_SIZE];

	PRINT_DUMP("-----------------------------------------------------\n");
	PRINT_DUMP(" Cache Dump : TLB\n");
	PRINT_DUMP("-----------------------------------------------------\n");

	PRINT_DUMP(" L1-I TLB array\n");
	for (entry_i = 0; entry_i < 48; entry_i++)	{
		/*
		 * 31   24                             5       0
		 * ----------------------------------------------
		 * |RAMID |              x            |TLB entry|
		 * ----------------------------------------------
		 */

		buf[0] = '\0';
		ramindex = (L1_I_TLB_RAM << 24) | entry_i;
		filled = get_tlb_data(buf, ramindex);

		if (!filled)
			continue;

		strncat(buf, "\n", BUFF_SIZE - 1 - strlen(buf));
		PRINT_DUMP("%s", buf);
	}

	PRINT_DUMP(" L1-D TLB array\n");
	for (entry_i = 0; entry_i < 32; entry_i++)	{
		/*
		 * 31   24                             5       0
		 * ----------------------------------------------
		 * |RAMID |              x            |TLB entry|
		 * ----------------------------------------------
		 */

		buf[0] = '\0';
		ramindex = (L1_D_TLB_RAM << 24) | entry_i;
		filled = get_tlb_data(buf, ramindex);

		if (!filled)
			continue;

		strncat(buf, "\n", BUFF_SIZE - 1 - strlen(buf));
		PRINT_DUMP("%s", buf);
	}
}

void dump_cache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	// L1_disable();

	__dump_L1_cache(INST_CACHE);	/* L1 I-cache dump */
	__dump_L1_cache(DATA_CACHE);	/* L1 D-cache dump */
	__dump_tlb_cache();		/* I/D-TLB dump */

	// L1_enable();
	preempt_enable();
	local_irq_restore(flags);
}

