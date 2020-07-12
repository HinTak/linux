/*
 * Cryptographic API.
 *
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>
#include <crypto/internal/compress.h>
#include <asm/unaligned.h>


#define ZSTD_DEF_LEVEL	1

static ZSTD_parameters params;

#ifdef CONFIG_BACKGROUND_RECOMPRESS
static ZSTD_parameters bgrecomp_params;
atomic64_t zstd_def_level;

void bgrecompress_set_comp_level(int comp_level)
{
	atomic64_set(&zstd_def_level, comp_level);
}

int bgrecompress_get_comp_level(void)
{
	int comp_level;

	if (test_thread_flag(TIF_BGRECOMPD))
		comp_level = (int)atomic64_read(&zstd_def_level);
	else
		comp_level = ZSTD_DEF_LEVEL;

	return comp_level;
}

int bgrecompress_get_recomp_level(void)
{
	return (int)atomic64_read(&zstd_def_level);
}
#endif

struct zstd_ctx {
	ZSTD_CCtx *cctx;
	ZSTD_DCtx *dctx;
	void *cwksp;
	void *dwksp;
};

static ZSTD_parameters zstd_params(void)
{
#ifndef CONFIG_BACKGROUND_RECOMPRESS
	return ZSTD_getParams(ZSTD_DEF_LEVEL, 0, 0);
#else
	return ZSTD_getParams(bgrecompress_get_comp_level(), 0, 0);
#endif
}

static int zstd_comp_init(struct zstd_ctx *ctx)
{
	int ret = 0;
	size_t wksp_size;

#ifndef CONFIG_BACKGROUND_RECOMPRESS
	params = zstd_params();
	wksp_size = ZSTD_CCtxWorkspaceBound(params.cParams);
#else
	if (!test_thread_flag(TIF_BGRECOMPD))
		params = zstd_params();
	else
		bgrecomp_params = zstd_params();

	wksp_size = ZSTD_CCtxWorkspaceBound(test_thread_flag(TIF_BGRECOMPD) ? bgrecomp_params.cParams : params.cParams);
#endif

	ctx->cwksp = vzalloc(wksp_size);
	if (!ctx->cwksp) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->cctx = ZSTD_initCCtx(ctx->cwksp, wksp_size);
	if (!ctx->cctx) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(ctx->cwksp);
	goto out;
}

static int zstd_decomp_init(struct zstd_ctx *ctx)
{
	int ret = 0;
	const size_t wksp_size = ZSTD_DCtxWorkspaceBound();

	ctx->dwksp = vzalloc(wksp_size);
	if (!ctx->dwksp) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->dctx = ZSTD_initDCtx(ctx->dwksp, wksp_size);
	if (!ctx->dctx) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(ctx->dwksp);
	goto out;
}

static void zstd_comp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->cwksp);
	ctx->cwksp = NULL;
	ctx->cctx = NULL;
}

static void zstd_decomp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->dwksp);
	ctx->dwksp = NULL;
	ctx->dctx = NULL;
}

static int __zstd_init(void *ctx)
{
	int ret;

	ret = zstd_comp_init(ctx);
	if (ret)
		return ret;
	ret = zstd_decomp_init(ctx);
	if (ret)
		zstd_comp_exit(ctx);
	return ret;
}

/* In order to not make repetition log output when decompress is failed during swap-in  */
struct dcomp_err_info {
	unsigned long *err_adrr;
	unsigned int err_source_length;
};

static DEFINE_PER_CPU(struct dcomp_err_info, prev_dcomp_err);

static int zstd_init(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_init(ctx);
}

static void __zstd_exit(void *ctx)
{
	zstd_comp_exit(ctx);
	zstd_decomp_exit(ctx);
}

static void zstd_exit(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	__zstd_exit(ctx);
}

static int __zstd_compress(const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;
	struct zstd_ctx *zctx = ctx;

#ifndef CONFIG_BACKGROUND_RECOMPRESS
	out_len = ZSTD_compressCCtx(zctx->cctx, dst, *dlen, src, slen, &params);
#else
	out_len = ZSTD_compressCCtx(zctx->cctx, dst, *dlen, src, slen, test_thread_flag(TIF_BGRECOMPD) ? &bgrecomp_params : &params);
#endif

	if (ZSTD_isError(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int zstd_compress(struct crypto_tfm *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_compress(src, slen, dst, dlen, ctx);
}

static int __zstd_decompress(const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;
	struct zstd_ctx *zctx = ctx;

	out_len = ZSTD_decompressDCtx(zctx->dctx, dst, *dlen, src, slen);
	/* repetition of making same error log is not allowed */
	if (ZSTD_isError(out_len)) {
		struct dcomp_err_info *prev_err = &get_cpu_var(prev_dcomp_err);
		if ((prev_err->err_adrr != (unsigned long *)src) && (prev_err->err_source_length != slen)) {
			/* refresh the previous error information */
			prev_err->err_adrr = (unsigned long *)src;
			prev_err->err_source_length = slen;

			pr_err("[zstd_decomp_fail][errCode:%s|magic:%u|src_magic:%u|src:0x%lx|slen:%u]",
					ZSTD_ErrorCode_names[ZSTD_getErrorCode(out_len)], ZSTD_MAGICNUMBER, get_unaligned_le32((const void *)src), (unsigned long int)src, slen);
			print_hex_dump(KERN_ERR,"", DUMP_PREFIX_ADDRESS, 16, 4, src, slen, true);
		}
		put_cpu_var(prev_dcomp_err);
		return -EINVAL;
	}
	*dlen = out_len;
	return 0;
}

static int zstd_decompress(struct crypto_tfm *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_decompress(src, slen, dst, dlen, ctx);
}

static struct crypto_alg alg = {
	.cra_name		= "zstd",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zstd_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= zstd_init,
	.cra_exit		= zstd_exit,
	.cra_u			= { .compress = {
	.coa_compress		= zstd_compress,
	.coa_decompress		= zstd_decompress } }
};

static int __init zstd_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);

	return ret;
}

static void __exit zstd_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(zstd_mod_init);
module_exit(zstd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zstd Compression Algorithm");
MODULE_ALIAS_CRYPTO("zstd");
