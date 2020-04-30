#ifndef _SDP_SMEM_CACHE_H
#define _SDP_SMEM_CACHE_H

struct gcma;

int smem_cache_init(unsigned long start_pfn, unsigned long size,
			struct gcma **res_cache);
int smem_cache_claim_area(struct gcma *cache, unsigned long start_pfn,
			unsigned long size);
void smem_cache_release_area(struct gcma *cache,
			unsigned long start_pfn, unsigned long size);

#endif /* _SDP_SMEM_CACHE_H */
