/*
 * Copyright (C) 2019
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/crypto.h>

#include "bgcomp.h"

static const char * const backends[] = {
	"lzo",
#if IS_ENABLED(CONFIG_CRYPTO_LZ4)
	"lz4",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_LZ4HC)
   "lz4hc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_842)
   "842",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_ZSTD)
    "zstd",
#endif
	NULL
};

static void bgcomp_strm_free(struct bgcomp_strm *zstrm)
{
	if (!IS_ERR_OR_NULL(zstrm->tfm))
		crypto_free_comp(zstrm->tfm);
	free_pages((unsigned long)zstrm->buffer, 1);
	kfree(zstrm);
}

/*
 * allocate new bgcomp_strm structure with ->tfm initialized by
 * backend, return NULL on error
 */
static struct bgcomp_strm *bgcomp_strm_alloc(struct bgcomp *comp)
{
	struct bgcomp_strm *zstrm = kmalloc(sizeof(*zstrm), GFP_KERNEL);
	if (!zstrm)
		return NULL;

	zstrm->tfm = crypto_alloc_comp(comp->name, 0, 0);
	/*
	 * allocate 2 pages. 1 for compressed data, plus 1 extra for the
	 * case when compressed size is larger than the original one
	 */
	zstrm->buffer = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (IS_ERR_OR_NULL(zstrm->tfm) || !zstrm->buffer) {
		bgcomp_strm_free(zstrm);
		zstrm = NULL;
	}

	return zstrm;
}

bool bgcomp_available_algorithm(const char *comp)
{
   int i = 0;

   while (backends[i]) {
       if (sysfs_streq(comp, backends[i]))
           return true;
       i++;
   }

   /*
    * Crypto does not ignore a trailing new line symbol,
    * so make sure you don't supply a string containing
    * one.
    * This also means that we permit bgcomp initialisation
    * with any compressing algorithm known to crypto api.
    */
   return crypto_has_comp(comp, 0, 0) == 1;
}

/* show available compressors */
ssize_t bgcomp_available_show(const char *comp, char *buf)
{
	bool known_algorithm = false;
	ssize_t sz = 0;
	int i = 0;

	for (; backends[i]; i++) {
		if (!strcmp(comp, backends[i])) {
			known_algorithm = true;
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"[%s] ", backends[i]);
		} else {
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"%s ", backends[i]);
		}
	}

	/*
	 * Out-of-tree module known to crypto api or a missing
	 * entry in `backends'.
	 */
	if (!known_algorithm && crypto_has_comp(comp, 0, 0) == 1)
		sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
				"[%s] ", comp);

	sz += scnprintf(buf + sz, PAGE_SIZE - sz, "\n");
	return sz;
}

struct bgcomp_strm *bgcomp_stream_get(struct bgcomp *comp)
{
	return comp->stream;
}

void bgcomp_stream_put(struct bgcomp *comp)
{
	return;
}

int bgcomp_compress(struct bgcomp_strm *zstrm,
		const void *src, unsigned int *dst_len)
{
	/*
	 * Our dst memory (zstrm->buffer) is always `2 * PAGE_SIZE' sized
	 * because sometimes we can endup having a bigger compressed data
	 * due to various reasons: for example compression algorithms tend
	 * to add some padding to the compressed buffer. Speaking of padding,
	 * comp algorithm `842' pads the compressed length to multiple of 8
	 * and returns -ENOSP when the dst memory is not big enough, which
	 * is not something that ZRAM wants to see. We can handle the
	 * `compressed_size > PAGE_SIZE' case easily in ZRAM, but when we
	 * receive -ERRNO from the compressing backend we can't help it
	 * anymore. To make `842' happy we need to tell the exact size of
	 * the dst buffer, zram_drv will take care of the fact that
	 * compressed buffer is too big.
	 */
	*dst_len = PAGE_SIZE * 2;

	return crypto_comp_compress(zstrm->tfm,
			src, PAGE_SIZE,
			zstrm->buffer, dst_len);
}

int bgcomp_decompress(struct bgcomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst)
{
	unsigned int dst_len = PAGE_SIZE;

	return crypto_comp_decompress(zstrm->tfm,
			src, src_len,
			dst, &dst_len);
}

static int bgcomp_init(struct bgcomp *comp)
{
	int ret = 0;

	comp->stream = bgcomp_strm_alloc(comp);
	if (!comp->stream)
		ret = -ENOMEM;

	init_rwsem(&comp->strm_lock);
	return ret;
}

void bgcomp_destroy(struct bgcomp *comp)
{
	bgcomp_strm_free(comp->stream);
	kfree(comp);
}

int bgcomp_level_reset(struct bgcomp *comp)
{
	int ret = 0;

	bgcomp_strm_free(comp->stream);
	comp->stream = bgcomp_strm_alloc(comp);
	if (!comp->stream)
		ret = -ENOMEM;

	return ret;
}

/*
 * search available compressors for requested algorithm.
 * allocate new bgcomp and initialize it. return compressing
 * backend pointer or ERR_PTR if things went bad. ERR_PTR(-EINVAL)
 * if requested algorithm is not supported, ERR_PTR(-ENOMEM) in
 * case of allocation error, or any other error potentially
 * returned by bgcomp_init().
 */
struct bgcomp *bgcomp_create(const char *compress)
{
	struct bgcomp *comp;
	int error;

	if (!bgcomp_available_algorithm(compress))
		return ERR_PTR(-EINVAL);

	comp = kzalloc(sizeof(struct bgcomp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	comp->name = compress;
	error = bgcomp_init(comp);
	if (error) {
		kfree(comp);
		return ERR_PTR(error);
	}
	return comp;
}
