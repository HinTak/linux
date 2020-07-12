#include <linux/delay.h>
#include "synopGMAC_Dev.h"
#include "nvt_phy.h"


void nvt_phy_reset(struct synop_device *gmacdev)
{
	/* Global Reset Digital Phy (EQ reset included). */
	ioremap_OR_value(0xFD130000, 0x1 << 1);

	/**
	 * Power sequence of 671D
	 *
	 * EN_BGR	0xFD1300F8[7] = 1
	 *	delay 20us
	 * EN_PLL_LDO	0xFD1300C8[0] = 0
	 *	delay 200us
	 * EN_PLL	0xFD1300C8[1] = 0
	 *	delay 250us
	 * EN_TX_DAC	0xFD1302E8[0] = 0
	 * EN_TX_LD	N.A. (Linked to EN_TX_DAC)
	 * EN_RX	0xFD1300CC[0] = 0
	 * EN_ADC	0xFD1300DC[0] = 1, 0xFD13009C[0] = 0
	 */
	ioremap_OR_value(0xFD1300F8,  (0x1 << 7));
	usleep_range(20, 21);
	ioremap_AND_value(0xFD1300C8, ~(0x1 << 0));
	usleep_range(200, 201);
	ioremap_AND_value(0xFD1300C8, ~(0x1 << 1));
	usleep_range(250, 251);
	ioremap_AND_value(0xFD1302E8, ~(0x1 << 0));
	ioremap_AND_value(0xFD1300CC, ~(0x1 << 0));
	ioremap_OR_value(0xFD1300DC,  (0x1 << 0));
	ioremap_AND_value(0xFD13009C, ~(0x1 << 0));

	/**
	 * Best setting
	 *	TX_DAC_RFR<2:0>  default 3'b000
	 *	FD130374[2:0] = 3'b101
	 *	TX_DAC_RFF<2:0>  default 3'b000
	 *	FD13037C[2:0] = 3'b000
	 *
	 * Farendloopback cdc option
	 * 	FD130074 = 0x80
	 */
	ioremap_OR_value(0xFD130374, (0x5 << 0));
	ioremap_OR_value(0xFD13037C, (0x0 << 0));
	ioremap_write(0xFD130074, 0x80);

	/**
	 * Best setting for 10M TX
	 *	Turn TX output waveform and
	 *	add the margin of 10Base-T MAU mask
	 */
	/* table 1 */
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xc0);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x2f);
	ioremap_write(0xfd130300, 0xc1);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x37);
	ioremap_write(0xfd130300, 0xc2);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x3d);
	ioremap_write(0xfd130300, 0xc3);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xc4);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xc5);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x3d);
	ioremap_write(0xfd130300, 0xc6);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x37);
	ioremap_write(0xfd130300, 0xc7);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x2f);
	ioremap_write(0xfd130300, 0xc8);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xc9);
	ioremap_write(0xfd130300, 0x00);
	/* table 2 */
	ioremap_write(0xfd130304, 0x1b);
	ioremap_write(0xfd130300, 0xca);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x11);
	ioremap_write(0xfd130300, 0xcb);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x09);
	ioremap_write(0xfd130300, 0xcc);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x03);
	ioremap_write(0xfd130300, 0xcd);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xce);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xcf);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x03);
	ioremap_write(0xfd130300, 0xd0);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x09);
	ioremap_write(0xfd130300, 0xd1);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x11);
	ioremap_write(0xfd130300, 0xd2);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x1b);
	ioremap_write(0xfd130300, 0xd3);
	ioremap_write(0xfd130300, 0x00);
	/* table 3 */
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xd4);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x2f);
	ioremap_write(0xfd130300, 0xd5);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x37);
	ioremap_write(0xfd130300, 0xd6);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x3d);
	ioremap_write(0xfd130300, 0xd7);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xd8);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xd9);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xda);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xdb);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x30);
	ioremap_write(0xfd130300, 0xdc);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x1b);
	ioremap_write(0xfd130300, 0xdd);
	ioremap_write(0xfd130300, 0x00);
	/* table 4 */
	ioremap_write(0xfd130304, 0x1b);
	ioremap_write(0xfd130300, 0xde);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x11);
	ioremap_write(0xfd130300, 0xdf);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x09);
	ioremap_write(0xfd130300, 0xe0);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x03);
	ioremap_write(0xfd130300, 0xe1);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xe2);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xe3);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xe4);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x00);
	ioremap_write(0xfd130300, 0xe5);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x10);
	ioremap_write(0xfd130300, 0xe6);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xe7);
	ioremap_write(0xfd130300, 0x00);
	/* table 5 */
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xe8);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xe9);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xea);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xeb);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xec);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x40);
	ioremap_write(0xfd130300, 0xed);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x3d);
	ioremap_write(0xfd130300, 0xee);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x37);
	ioremap_write(0xfd130300, 0xef);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x2f);
	ioremap_write(0xfd130300, 0xf0);
	ioremap_write(0xfd130300, 0x00);
	ioremap_write(0xfd130304, 0x25);
	ioremap_write(0xfd130300, 0xf1);
	ioremap_write(0xfd130300, 0x00);

	/*Global Reset Digital Phy (EQ reset included).*/
	msleep(1);
	ioremap_write(0xFD130000, 0x0);
}

