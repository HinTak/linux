#ifndef __LINUX_USB_NT72668_H
#define __LINUX_USB_NT72668_H

#include <mach/clk.h>

struct nt72668_platform_data {
    EN_SYS_CLK_RST  ahb_usb20;
    EN_SYS_CLK_RST  ahb_usb20_pclk;
    EN_SYS_CLK_RST  axi_usb20;
    EN_SYS_CLK_RST  core_usb20;
    EN_SYS_CLK_SRC  clk_src;
};

#endif /* __LINUX_USB_NT72668_H */
