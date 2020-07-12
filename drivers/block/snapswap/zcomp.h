/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

struct scomp_strm {
	/* compression/decompression buffer */
	void *buffer;
	struct crypto_comp *tfm;
};

/* dynamic per-device compression frontend */
struct scomp {
	struct scomp_strm * __percpu *stream;
	struct notifier_block notifier;
	const char *name;
};

ssize_t scomp_available_show(const char *comp, char *buf);
bool scomp_available_algorithm(const char *comp);

struct scomp *scomp_create(const char *comp);
void scomp_destroy(struct scomp *comp);

struct scomp_strm *scomp_stream_get(struct scomp *comp);
void scomp_stream_put(struct scomp *comp);

int scomp_compress(struct scomp_strm *zstrm,
		const void *src, unsigned int *dst_len);

int scomp_decompress(struct scomp_strm *zstrm,
		const void *src, unsigned int src_len, void *dst);

bool scomp_set_max_streams(struct scomp *comp, int num_strm);
#endif /* _ZCOMP_H_ */
