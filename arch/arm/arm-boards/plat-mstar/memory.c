/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * PROM library functions for acquiring/using memory descriptors given to
 * us from the YAMON.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/string.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/sections.h>
#include <asm/arm-boards/prom.h>

/*-----------------------------------------------------------------------------
    Include Files
------------------------------------------------------------------------------*/
unsigned long get_BBAddr(void);

# define NR_BANKS 8

struct membank {
        unsigned long start;
        unsigned long size;
        unsigned int highmem;
};

struct meminfo {
        int nr_banks;
        struct membank bank[NR_BANKS];
};

extern struct meminfo meminfo;

//2010/10/05 modify by mstar: hardcode EMACmem without getting from bootcommand
static unsigned long LXmem2Addr=0x0, LXmem2Size=0x0, LXmemAddr=0x0, LXmemSize=0x0, EMACaddr=0x0, EMACmem=0x0, DRAMlen=0x0, BBAddr=0x0; //MPoolSize=0x0;
unsigned long lx_mem_addr = 0x40200000;
unsigned long lx_mem_size=0x20000000;
unsigned long lx_mem2_addr=0xA0000000;
EXPORT_SYMBOL(lx_mem_addr);
EXPORT_SYMBOL(lx_mem_size);
EXPORT_SYMBOL(lx_mem2_addr);

extern int  arm_add_memory(unsigned long start, unsigned long size);
//2011/8/17 modified by mstar: add LX_MEM function to dynamically change Linux memory address and size 
static int __init LX_MEM_setup(char *str)
{
    //printk("LX_MEM= %s\n", str);
    if( str != NULL )
    {
        sscanf(str,"%lx,%lx",&LXmemAddr,&LXmemSize);
        meminfo.nr_banks = 0;
        lx_mem_addr = LXmemAddr;
        lx_mem_size = LXmemSize;
    }
    else
    {
        printk("\nLX_MEM not set\n");
    }

    if (LXmemSize != 0)
        arm_add_memory(LXmemAddr, LXmemSize);

    return 0;
}
//2010/10/05 Removed by mstar: Remove un-used functions: EMAC_MEM_setup  DRAM_LEN_setup  MPoolSize_setup,
//Because we no longer use the return value of above function
static int __init LX_MEM2_setup(char *str)
{
    //printk("LX_MEM2= %s\n", str);
    if( str != NULL )
    {
        sscanf(str,"%lx,%lx",&LXmem2Addr,&LXmem2Size);
        lx_mem2_addr = LXmem2Addr;
    }
    else
    {
        printk("\nLX_MEM2 not set\n");
    }

    if (LXmem2Size != 0)
        arm_add_memory(LXmem2Addr, LXmem2Size);

    return 0;
}

//2011/8/17 modified by mstar: add EMAC_setup function to dynamically change EMAC driver address and  size 
static int __init EMAC_MEM_setup(char *str)
{
    //printk("LX_MEM= %s\n", str);
    if( str != NULL )
    {
        sscanf(str,"%lx,%lx",&EMACaddr,&EMACmem);
    }
    else
    {
        printk("\nEMMAC not set\n");
    }
    return 0;
}


static int __init BBAddr_setup(char *str)
{
    //printk("LX_MEM2= %s\n", str);
    if( str != NULL )
    {
        sscanf(str,"%lx",&BBAddr);
    }

    return 0;
}

//2010/10/05 removed by mstar: Remove no longer used paramater got from bootcommand
early_param("LX_MEM", LX_MEM_setup);
early_param("LX_MEM2", LX_MEM2_setup);
early_param("EMAC_MEM", EMAC_MEM_setup);
early_param("BB_ADDR", BBAddr_setup);
 
void get_boot_mem_info(BOOT_MEM_INFO type, unsigned int *addr, unsigned int *len)
{
        switch (type)
        {
//2011/8/17 modified by mstar: Modify valiable for dynamically passing arguments
        case LINUX_MEM:
            *addr = LXmemAddr;
            *len = LXmemSize & ~(0x00100000UL - 1) ;
            break;

        case EMAC_MEM:
            *addr = EMACaddr;
            *len = EMACmem & ~(0x00100000UL - 1) ;
            break;

        case LINUX_MEM2:
            if (LXmem2Addr!=0 && LXmem2Size!=0)
            {
                *addr = LXmem2Addr;
                *len = LXmem2Size & ~(0x00100000UL - 1) ;
            }
            else
            {
                *addr = 0;
                *len = 0;
            }
            break;

        default:
            *addr = 0;
            *len = 0;
            break;
        }
}
EXPORT_SYMBOL(get_boot_mem_info);

void __init prom_meminit(void)
{

    unsigned int linux_memory_address = 0, linux_memory_length = 0;
    unsigned int linux_memory2_address = 0, linux_memory2_length = 0;
    unsigned int linux_memory3_address = 0, linux_memory3_length = 0;

    get_boot_mem_info(LINUX_MEM, &linux_memory_address, &linux_memory_length);
    get_boot_mem_info(LINUX_MEM2, &linux_memory2_address, &linux_memory2_length);

    printk("\n");
    if(linux_memory_length > 0x10000000)
       printk("LX_MEM  = 0x%X, 0x%X\n", linux_memory_address,0x10000000);
    else
    printk("LX_MEM  = 0x%X, 0x%X\n", linux_memory_address,linux_memory_length);
    printk("LX_MEM2 = 0x%X, 0x%X\n",linux_memory2_address, linux_memory2_length);
    if(linux_memory_length > 0x10000000)
    printk("LX_MEM3 = 0x%X, 0x%X\n",linux_memory3_address, linux_memory3_length);
    //printk("CPHYSADDR(PFN_ALIGN(&_end))= 0x%X\n", CPHYSADDR(PFN_ALIGN(&_end)));
    printk("EMACaddr= 0x%lX\n", EMACaddr);
    printk("EMACmem= 0x%lX\n", EMACmem);
    printk("DRAM_LEN= 0x%lX\n", DRAMlen);
}

inline unsigned long get_BBAddr(void)
{
    return BBAddr;
}

//void __init prom_free_prom_memory(void)
//{
//    unsigned long addr;
//    int i;
//
//    for (i = 0; i < boot_mem_map.nr_map; i++) {
//        if (boot_mem_map.map[i].type != BOOT_MEM_ROM_DATA)
//            continue;
//
//        addr = boot_mem_map.map[i].addr;
//        free_init_pages("prom memory",
//                addr, addr + boot_mem_map.map[i].size);
//    }
//}
