#ifndef __NVT_SHA256_HAL_H
#define __NVT_SHA256_HAL_H
int hw_auth_start(dma_addr_t *ipages, int nr_ipages, unsigned int input_bytes, struct nvt_unzip_auth_t *auth, u32 offset);
int hw_auth_wait(void);
void hw_auth_update_endpointer(void);
#endif
