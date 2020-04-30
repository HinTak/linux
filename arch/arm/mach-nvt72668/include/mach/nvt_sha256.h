#ifndef __NVT_SHA256_HAL_H
#define __NVT_SHA256_HAL_H

void calculate_hw_hash_sha256(unsigned char *buf,
		dma_addr_t pbuff, unsigned int buf_len);
static inline void calculate_hw_hash(unsigned long vstart,
		unsigned long pstart, unsigned long length)
{
#ifdef CONFIG_NVT_HW_SHA256
	/* NVT provides H/W SHA256 */
	calculate_hw_hash_sha256((unsigned char *)vstart, (dma_addr_t)pstart,
			(unsigned int)length);
#endif
}
int hw_sha256_wait(unsigned char* hash);

#endif
