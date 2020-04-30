/*
 * crypto/crypto_wrapper.c
 * 
 * Copyright (C) 2015 by Samsung Electronics, Inc.
 *
 * Wrapper function for hash/verification buddies.
 * 
 * Previously, hash/verification functions are duplicated on each layer
 * and implemented with own way for each target.
 * Even file system and decompressor has own hash/verification function.
 * We want to eliminate this situation and resolve porting/migration issue.
 * This code mainly comes from vdfs authentication part and latest linux mainline.
 * 
 * TODO : Since crypto/rsa.c is not matured this kernel(v3.10.28),
 *        we implement own rsa verfication code.
 *        In further kernel, we need to change it to use rsa code of mainline.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/crypto.h>
#include <linux/mpi.h>
#include <crypto/hash.h>
#include <crypto/crypto_wrapper.h>

static int calculate_sw_hash(unsigned char *buf, size_t buf_len,
        unsigned char *hash, const char *hash_type)
{
    int ret = 0;
    unsigned int size;
    struct crypto_shash *cshash;
    struct shash_desc *shdesc;
    cshash = crypto_alloc_shash(hash_type, 0, 0);
    if (IS_ERR(cshash))
        return PTR_ERR(cshash);

    size = sizeof(struct shash_desc) + crypto_shash_descsize(cshash);
    shdesc = kmalloc(size, GFP_NOFS);
    if (!shdesc) {
        crypto_free_shash(cshash);
        return -ENOMEM;
    }

    shdesc->tfm = cshash;
    shdesc->flags = 0x0;
    ret = crypto_shash_init(shdesc);
    if (ret)
        goto exit;
    ret = crypto_shash_update(shdesc, buf, buf_len);
    if (ret)
        goto exit;
    ret = crypto_shash_final(shdesc, hash);
exit:
    crypto_free_shash(cshash);
    kfree(shdesc);
    return ret;
}

int calculate_sw_hash_md5(unsigned char *buf, size_t buf_len,
        char *hash)
{
    const char md5[] = "md5";
#ifndef CONFIG_CRYPTO_MD5
	pr_err("Can't calculate hash by md5. CONFIG_CRYPTO_MD5 is not set");
	return -EINVAL;
#endif
    return calculate_sw_hash(buf, buf_len, hash, md5);
}

int calculate_sw_hash_sha256(unsigned char *buf, size_t buf_len,
        char *hash)
{
    const char sha256[] = "sha256";
#ifndef CONFIG_CRYPTO_SHA256
    pr_err("Can't calculate hash by sha256. CONFIG_CRYPTO_SHA256 is not set");
    return -EINVAL;
#endif
    return calculate_sw_hash(buf, buf_len, hash, sha256);
}

int calculate_sw_hash_sha1(unsigned char *buf, size_t buf_len,
        char *hash)
{
#ifdef  CONFIG_CRYPTO_SHA1_ARM_NEON
    const char sha1[] = "sha1-neon";
#else
    const char sha1[] = "sha1";
#endif
#ifndef CONFIG_CRYPTO_SHA1
    pr_err("Can't calculate hash by sha1. CONFIG_CRYPTO_SHA1 is not set");
    return -EINVAL;
#endif
    return calculate_sw_hash(buf, buf_len, hash, sha1);
}

void destroy_rsa_key(rsakey_t *pkey)
{
	if (pkey) {
		if (pkey->rsa_n)
			mpi_free(pkey->rsa_n);
		if (pkey->rsa_e)
			mpi_free(pkey->rsa_e);
		kfree(pkey);
	}
}

rsakey_t *create_rsa_key(const char *pub_rsa_n, const char *pub_rsa_e,
		size_t n_size, size_t e_size)
{
	rsakey_t *pkey = kzalloc(sizeof(rsakey_t), GFP_KERNEL);
	if (!pkey)
		return NULL;
	pkey->rsa_n = mpi_read_raw_data(pub_rsa_n, n_size);
	pkey->rsa_e = mpi_read_raw_data(pub_rsa_e, e_size);
	if (!pkey->rsa_n || !pkey->rsa_e) {
		destroy_rsa_key(pkey);
		return NULL;
	}
	return pkey;
}

static int rsa_check_sign_padding(const uint8_t* sign, uint32_t sign_len, 
		uint32_t data_len, int padding_type)
{
	unsigned int padding_end = sign_len - data_len - 1;
	int i;

	if ((sign_len > 0) && (sign_len == data_len) && 
		(padding_type & RSA_PADDING_PKCS1_TYPE0))
		//if case of PKCS1 type 0, all beginning zeros were removed by 
		//mpi_get_buffer. In that case signature and data length will be equal
		return 0;

	//minimal allowed padding is 01 FF FF FF FF FF FF FF FF 00
	if (sign_len < data_len + 10)
		return -EINVAL;  

	if ((0x01 != sign[0]) || (0x00 != sign[padding_end]))
		return -EPERM;

	for (i=1; i < padding_end; i++)
		if (0xff != sign[i])
			return -EPERM;

	return 0;
}

int rsa_check_signature(const rsakey_t *pkey,
		const uint8_t *signature, uint32_t sign_len,
		uint8_t *hash, uint32_t hash_len, int padding_type)
{		
	int ret = -EINVAL;
	MPI m_sign = NULL, m_em = NULL;

	uint8_t *em = NULL;
	uint32_t em_len = 0;

	m_em = mpi_alloc(0);
	if (!m_em) {
		ret = -ENOMEM;
		goto err;
	}

	m_sign = mpi_read_raw_data(signature, sign_len);
	if (!m_sign) {
		ret = -ENOMEM;
		goto err;
	}

	if (mpi_powm(m_em, m_sign, pkey->rsa_e, pkey->rsa_n)) {
		pr_err( "failed to perform modular exponentiation");
		goto err;
	}
	em = mpi_get_buffer(m_em, &em_len, NULL);
	if (!em) {
		pr_err("failed to get MPI buffer");
		goto err;
	}

	if (rsa_check_sign_padding(em, em_len, hash_len, padding_type)) {
		pr_err("rsa signature padding is incorrect");
		goto err2;
	}
	
	if (!memcmp(hash, em + em_len - hash_len, hash_len))
		ret = 0;

err2:
	kfree(em);
err:
	mpi_free(m_sign);
	mpi_free(m_em);
	return ret;
}

