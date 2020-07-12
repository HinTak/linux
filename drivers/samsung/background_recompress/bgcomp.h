/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _BGCOMP_H_
#define _BGCOMP_H_

struct bgcomp_strm {
	/* compression/decompression buffer */
	void *buffer;
	struct crypto_comp *tfm;
};

/* dynamic per-device compression frontend */
struct bgcomp {
	struct bgcomp_strm *stream;
	struct rw_semaphore strm_lock;

	const char *name;
};

ssize_t bgcomp_available_show(const char *comp, char *buf);
bool bgcomp_available_algorithm(const char *comp); 

struct bgcomp *bgcomp_create(const char *comp);
void bgcomp_destroy(struct bgcomp *comp);
int bgcomp_level_reset(struct bgcomp *comp);

struct bgcomp_strm *bgcomp_stream_get(struct bgcomp *comp);
void bgcomp_stream_put(struct bgcomp *comp);

int bgcomp_compress(struct bgcomp_strm *zstrm,
		const void *src, unsigned int *dst_len);

int bgcomp_decompress(struct bgcomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst);

bool bgcomp_set_max_streams(struct bgcomp *comp, int num_strm);
#endif /* _BGCOMP_H_ */
