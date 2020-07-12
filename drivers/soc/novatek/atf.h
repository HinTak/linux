/****************************************************************************
*
* atf.c (Novateck AHB TO AXI fifo driver)
*
*	author : jianhao.su@novatek.com.tw
*
* 2016/07/14, jianhao.su: novatek implementation for Samsung platform.
*
***************************************************************************/
enum iff_src {
	iff_NONE,
	iff_SPI,
	iff_NFC,
	iff_AES,
	iff_NAND_ARM,
};

enum iff_dest {
	to_ZIP = 32,
	to_AES = 64,
	to_SHA = 128,
};

struct atf_setting {
	unsigned int offset;
	unsigned int timeout;
	unsigned int src;
	unsigned int length;
	unsigned int dest;
};

void nvt_atf_execute(int length, int target, int offset);
void fifo_atf_done(void);
void fifo_regdump(void);
