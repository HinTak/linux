/*
 *  linux/arch/arm/mach-mb8ac0300/include/mach/ebus.h
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

#ifndef __MACH_EBUS_H
#define __MACH_EBUS_H

#define EBUS_SUCCESS		0
#define EBUS_EPCI_CA		10001
#define EBUS_EPCI_UR		10002
#define EBUS_EPCI_CT		10004
#define EBUS_EPCI_CRS		10007

#define EBUS_BYTE		1
#define EBUS_WORD		2
#define EBUS_DWORD		4

#define EBUS_IO_ADDRESS(n) \
	(((MB8AC0300_UDL_PHYS) <= (n)) && \
	 ((n) < ((MB8AC0300_UDL_PHYS) + ((MB8AC0300_UDL_SIZE))))) \
	? ((n) - (MB8AC0300_UDL_PHYS)  + (MB8AC0300_UDL_VIRT)) \
	: 0

/* functions --------------------------------------------------------------- */

void sdp_config_init(void);

int sdp_write_config_byte(unsigned int where, u8 val);

int sdp_write_config_word(unsigned int where, u16 val);

int sdp_write_config_dword(unsigned int where, u32 val);

int sdp_read_config_byte(unsigned int where, u8 *val);

int sdp_read_config_word(unsigned int where, u16 *val);

int sdp_read_config_dword(unsigned int where, u32 *val);

void sdp_multi_write(void *cpu_addr, void *dev_addr, size_t size);

void sdp_multi_read(void *cpu_addr, void *dev_addr, size_t size);

void __sdp_write_mem(void *vir_addr, unsigned long byte_data,
							unsigned long val);
void __sdp_read_mem(void *vir_addr, unsigned long byte_data,
							unsigned long *val);

static inline void sdp_writeb(void *addr, unsigned char data)
{
	__sdp_write_mem( addr, EBUS_BYTE, (unsigned long)data );
}

static inline void sdp_writew(void *addr, unsigned short data)
{
	__sdp_write_mem( addr, EBUS_WORD, (unsigned long)data );
}

static inline void sdp_writel(void *addr, unsigned long data)
{
	__sdp_write_mem( addr, EBUS_DWORD, (unsigned long)data );
}

static inline unsigned char sdp_readb(void *addr)
{
	unsigned long data;
	__sdp_read_mem( addr, EBUS_BYTE, &data );
	return (unsigned char)data;
}

static inline unsigned short sdp_readw(void *addr)
{
	unsigned long data;
	__sdp_read_mem( addr, EBUS_WORD, &data );
	return (unsigned short)data;
}

static inline unsigned long sdp_readl(void *addr)
{
	unsigned long data;
	__sdp_read_mem( addr, EBUS_DWORD, &data );
	return (unsigned long)data;
}

#endif /* __MACH_EBUS_H */
