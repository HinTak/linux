#include <linux/delay.h>
#include "synopGMAC_Dev.h"
#include "m_internal_Phy.h"
#include "nvt_phy.h"


void nvt_phy_reset(struct synop_device *gmacdev)
{
	u16 data;

	/* Global Reset Digital Phy (EQ reset included). */
	ioremap_OR_value(0xFD130000, 0x2);

	/*Setting of Analog Phy.*/
	ioremap_OR_value(0xFD13009C, 0x00000001);	/*Power down - ADC.*/

	ioremap_OR_value(0xFD1302E0, 0x00000010);	/*Power down - DAC.*/
	/*Power down - LDRV (Line driver) 1/0.*/
	ioremap_OR_value(0xFD1302C0, 0x00000003);

	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,
			       gmacdev->PhyBase,
				PHY_CONTROL_REG,
				&data);
	data |= Mii_reset;
	synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,
				gmacdev->PhyBase,
				PHY_CONTROL_REG,
				data);

	ioremap_OR_value(0xFD1300F8, 0x000000E0);	/*Power on - LDO*/
	usleep_range(1 * 1000, 2 * 1000);

	ioremap_OR_value(0xFD1300C8, 0x00000001);	/*Power on - PLL0*/
	ioremap_OR_value(0xFD130088, 0x00000001);
	usleep_range(1 * 1000, 2 * 1000);

	ioremap_OR_value(0xFD1300C4, 0x00000010);	/*Power on - PLL1*/
	ioremap_OR_value(0xFD130084, 0x00000010);
	usleep_range(1 * 1000, 2 * 1000);

	/* Power on RX ATT Power */
	ioremap_OR_value(0xFD1300CC, 0x00000001);
	ioremap_AND_value(0xFD13008C, (u32)(~(0x00000001)));

	/* Power on 100M Base RX Power (PGA) */
	ioremap_OR_value(0xFD1300D8, 0x00000001);
	ioremap_AND_value(0xFD130098, (u32)(~(0x00000001)));

	/* Power on 10M Base RX Power */
	ioremap_OR_value(0xFD1300D0, 0x00000001);
	ioremap_AND_value(0xFD130090, (u32)(~(0x00000001)));

	/* Set 2.5V LDO */
	ioremap_write(0xFD1300C4, 0xF);
	ioremap_OR_value(0xFD130084, 0x00000008);

	/* Power on ADC Power */
	ioremap_OR_value(0xFD1300DC, 0x00000001);
	ioremap_AND_value(0xFD13009C, (u32)(~(0x00000001)));
	usleep_range(1 * 1000, 2 * 1000);

	/* ADC common mode Voltage setting. */
	ioremap_write(0xFD130104, 0xEC);

	/* Power on DAC and LDRV (Line driver) 1/0. */
	ioremap_OR_value(0xFD1302E8, 0x00000007);
	ioremap_AND_value(0xFD1302E0, (u32)(~(0x00000010)));
	ioremap_AND_value(0xFD1302C0, (u32)(~(0x00000003)));
	/*Setting of Analog Phy.*/

	/* Auto-MDIX */
	ioremap_write(0xFD130288, 0x01);
	ioremap_write(0xFD130284, 0xDB);

	/*Global Reset Digital Phy (EQ reset included).*/
	ioremap_write(0xFD130000, 0x0);

	synopGMAC_set_nvt_phy_mii(gmacdev);
	synopGMAC_set_nvt_phy_link_trigger_level(gmacdev);
}

