/*
 * z4fold memory allocator
 *
 * Copyright (C) 2017  jusun song
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the license that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#ifndef _Z4FOLD_H_
#define _Z4FOLD_H_

#include <linux/types.h>

/*
 * z4fold mapping modes
 *
 * NOTE: These only make a difference when a mapped object spans pages.
 * They also have no effect when PGTABLE_MAPPING is selected.
 */
enum z4fold_mapmode {
	Z4_MM_RW, /* normal read-write mapping */
	Z4_MM_RO, /* read-only (no copy-out at unmap time) */
	Z4_MM_WO /* write-only (no copy-in at map time) */
	/*
	 * NOTE: ZS_MM_WO should only be used for initializing new
	 * (uninitialized) allocations.  Partial writes to already
	 * initialized allocations should use ZS_MM_RW to preserve the
	 * existing data.
	 */
};

struct z4fold_pool;
struct z4fold_ops;

struct z4fold_pool *z4fold_create_pool(const char *name, gfp_t gfp,          
        const struct z4fold_ops *ops);  
void z4fold_destroy_pool(struct z4fold_pool *pool);

int z4fold_alloc(struct z4fold_pool *pool, size_t size, gfp_t gfp,
		unsigned long *handle);

void z4fold_free(struct z4fold_pool *pool, unsigned long handle);

void *z4fold_map(struct z4fold_pool *pool, unsigned long handle, enum z4fold_mapmode mm);
void z4fold_unmap(struct z4fold_pool *pool, unsigned long handle);

u64 z4fold_get_pool_size(struct z4fold_pool *pool);
u64 z4fold_get_headless_pages(struct z4fold_pool *pool);
#endif
