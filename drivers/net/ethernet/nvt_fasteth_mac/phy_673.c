#include <linux/delay.h>
#include "synopGMAC_Dev.h"
#include "nvt_phy.h"


void nvt_phy_reset(struct synop_device *gmacdev)
{
	/* Global Reset Digital Phy (EQ reset included). */
	ioremap_OR_value(0xFD130000, 0x1 << 1);

	/**
	 * EN_BGR	FD1300F8[7] = 1
	 * EN_TX_DAC	FD1302E8[0] = 0
	 * EN_TX_LD	FD130390[4] = 0
	 * EN_RX	FD1300CC[0] = 0
	 * EN_ADC	FD1300DC[0] = 1, FD13009C[0] = 0
	 * 	delay 0.8us
	 * EN_PLL_LDO	FD1300C8[0] = 0
	 *	delay 200us
	 * EN_PLL	FD1300C8[1] = 0
	 */
	ioremap_OR_value( 0xFD1300F8,  (0x1 << 7));
	ioremap_AND_value(0xFD1302E8, ~(0x1 << 0));
	ioremap_AND_value(0xFD130390, ~(0x1 << 4));
	ioremap_AND_value(0xFD1300CC, ~(0x1 << 0));
	ioremap_OR_value( 0xFD1300DC,  (0x1 << 0));
	ioremap_AND_value(0xFD13009C, ~(0x1 << 0));
	usleep_range(1, 2);
	ioremap_AND_value(0xFD1300C8, ~(0x1 << 0));
	usleep_range(200, 201);
	ioremap_AND_value(0xFD1300C8, ~(0x1 << 1));

	/**
	 * Best setting:
	 *	ADC_VLDO<2:0> = 3'b011, FD1302F8[2:0]
	 *	TST_PLL_CLK_EN = 1, FD130354[7]
	 *	TST_PLL_CLK_SEL<1:0>=11, FD130358[5:4]
	 */
	ioremap_AND_value(0xFD1302F8, ~(0x7 << 0));
	ioremap_OR_value( 0xFD1302F8,  (0x3 << 0));
	ioremap_OR_value( 0xFD130354,  (0x1 << 7));
	ioremap_OR_value( 0xFD130358,  (0x3 << 4));

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

