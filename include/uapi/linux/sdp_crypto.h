/*
 * crypto.h
 *
 * This source file is proprietary property of Samsung Electronics Co., Ltd.
 *
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Contact: Jaemin Ryu <jm77.ryu@samsung.com>
 *
 */

#ifndef _CRYPTO_INFO_
#define _CRYPTO_INFO_

#ifdef CONFIG_ARCH_SDP1106
/*****************************************************************
	Definition - Mechanism
*****************************************************************/
#define IOCTL_CRYPTO_INIT	0
#define IOCTL_CRYPTO_CRYPT	1
#define IOCTL_CRYPTO_CRYPT_WITH_PHYS_ADDR	2


#define IOCTL_CRYPTO_GET_DEVICE_ID		5
#define IOCTL_CRYPTO_GET_LAST_BLOCK	6
#define IOCTL_CRYPTO_IMAGE_LOAD		7

#define IOCTL_CRYPTO_USER_FUNC_0	10
#define IOCTL_CRYPTO_USER_FUNC_1	11
#define IOCTL_CRYPTO_USER_FUNC_2	12
#define IOCTL_CRYPTO_USER_FUNC_3	13
#define IOCTL_CRYPTO_USER_FUNC_4	14

#define KEYID_MASTER_KEY 1
#define KEYID_SECURE_KEY 2
#define KEYID_USER_KEY 3

#define BC_MODE_ENC	0
#define BC_MODE_DEC	1

/*
 *	Mechanism ID definition
 *	: Mech. Type (8-bit) : Algorithm (8-bit) : Info (8-bit) : Reserved (8-bit)
 */
#define _MECH_ID_(_TYPE_, _NAME_, _MODE_)	\
	((((_TYPE_) & 0xFF) << 24) | 		\
	 (((_NAME_) & 0xFF) << 16) |		\
	 (((_MODE_) & 0xFF) << 8)  |		\
	 (((0) & 0xFF) << 0))

#define MI_NOT_SUPPORT		0
#define MI_MASK			_MECH_ID_(0xFF, 0xFF, 0xFF)
#define MI_GET_TYPE(_t_)	(((_t_) >> 24) & 0xFF)
#define MI_GET_NAME(_n_)	(((_n_) >> 16) & 0xFF)
#define MI_GET_INFO(_i_)	(((_i_) >> 8) & 0xFF)

/* type (8-bits) */
#define _TYPE_BC_		0x01
#define _TYPE_HASH_		0x02
#define _TYPE_MAC_		0x03
#define _TYPE_PRNG_		0x04

/* block cipher: algorithm (8-bits) */
#define _NAME_DES_		0x01
#define _NAME_TDES_		0x02
#define _NAME_AES_		0x03
#define _NAME_RSA_      0x04


/* block cipher: mode of operation */
#define _MODE_ECB_		0x10
#define _MODE_CBC_		0x20
#define _MODE_CTR_		0x30
#define _MODE_XTS_		0x40

/* block cipher: padding method */
#define _PAD_NO_		0x00
#define _PAD_ZERO_		0x01
#define _PAD_PKCS7_		0x02
#define _PAD_ANSIX923_	0x03
#define _PAD_ISO10126_	0x04

#define MI_GET_MODE(_m_)	( ((_m_) >> 8) & 0xF0 )
#define MI_GET_PADDING(_i_)	( ((_i_) >> 8) & 0x0F )

#define MI_AES_ECB		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_ECB_ | _PAD_NO_)
#define MI_AES_ECB_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_ECB_ | _PAD_PKCS7_)
#define MI_AES_CBC		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_CBC_ | _PAD_NO_)
#define MI_AES_CBC_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_CBC_ | _PAD_PKCS7_)
#define MI_AES_CTR		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_CTR_ | _PAD_NO_)
#define MI_AES_CTR_PAD		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_CTR_ | _PAD_PKCS7_)
#define MI_AES_XTS		_MECH_ID_(_TYPE_BC_, _NAME_AES_, _MODE_XTS_ | _PAD_NO_) 

#define MI_DES_ECB		_MECH_ID_(_TYPE_BC_, _NAME_DES_, _MODE_ECB_ | _PAD_NO_)
#define MI_DES_ECB_PAD		_MECH_ID_(_TYPE_BC_, _NAME_DES_, _MODE_ECB_ | _PAD_PKCS7_)
#define MI_DES_CBC		_MECH_ID_(_TYPE_BC_, _NAME_DES_, _MODE_CBC_ | _PAD_NO_)
#define MI_DES_CBC_PAD		_MECH_ID_(_TYPE_BC_, _NAME_DES_, _MODE_CBC_ | _PAD_PKCS7_)

#define MI_TDES_ECB		_MECH_ID_(_TYPE_BC_, _NAME_TDES_, _MODE_ECB_ | _PAD_NO_)
#define MI_TDES_ECB_PAD		_MECH_ID_(_TYPE_BC_, _NAME_TDES_, _MODE_ECB_ | _PAD_PKCS7_)
#define MI_TDES_CBC		_MECH_ID_(_TYPE_BC_, _NAME_TDES_, _MODE_CBC_ | _PAD_NO_)
#define MI_TDES_CBC_PAD		_MECH_ID_(_TYPE_BC_, _NAME_TDES_, _MODE_CBC_ | _PAD_PKCS7_)

/* hash & HMAC : */
#define _NAME_HASH_MD5_		0x01
#define _NAME_HASH_SHA1_		0x02
#define _NAME_HASH_SHA224_	0x03
#define _NAME_HASH_SHA256_	0x04
#define _NAME_HASH_SHA384_	0x05
#define _NAME_HASH_SHA512_	0x06

#define MI_MD5			_MECH_ID_(_TYPE_HASH_, _NAME_HASH_MD5_, 0)
#define MI_SHA1			_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA1_, 0)
#define MI_SHA224		_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA224_, 0)
#define MI_SHA256		_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA256_, 0)
#define MI_SHA384		_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA384_, 0)
#define MI_SHA512		_MECH_ID_(_TYPE_HASH_, _NAME_HASH_SHA512_, 0)

#define MI_HMAC_MD5		_MECH_ID_(_TYPE_MAC_, _NAME_HASH_MD5_, 0)
#define MI_HMAC_SHA1		_MECH_ID_(_TYPE_MAC_, _NAME_HASH_SHA1_, 0)
#define MI_HMAC_SHA224	_MECH_ID_(_TYPE_MAC_, _NAME_HASH_SHA224_, 0)
#define MI_HMAC_SHA256	_MECH_ID_(_TYPE_MAC_, _NAME_HASH_SHA256_, 0)
#define MI_HMAC_SHA384	_MECH_ID_(_TYPE_MAC_, _NAME_HASH_SHA384_, 0)
#define MI_HMAC_SHA512	_MECH_ID_(_TYPE_MAC_, _NAME_HASH_SHA512_, 0)

/* Random : */
#define MI_PRNG			_MECH_ID_(_TYPE_PRNG_, 0, 0)

/*
 *	Flag bits
 */
#define FLAG_ENC_BIT		(1 << 0)

#define _MODE_ENC_		(0 << 0)
#define _MODE_DEC_		(1 << 0)
#define MI_IS_DEC(m) 		((m) & _MODE_DEC_)
////////////

#define _CBC_MAC_				(0x02)  
#define MI_IS_CBC_MAC(m) 		((m) & _CBC_MAC_)

#define MI_DES_CBC_MAC		(MI_DES_CBC | _CBC_MAC_)
#define MI_DES_CBC_PAD_MAC	(MI_DES_CBC_PAD | _CBC_MAC_)
#define MI_AES_CBC_MAC		(MI_AES_CBC | _CBC_MAC_)
#define MI_AES_CBC_PAD_MAC	(MI_AES_CBC_PAD | _CBC_MAC_)
#define MI_TDES_CBC_MAC		(MI_TDES_CBC | _CBC_MAC_)
#define MI_TDES_CBC_PAD_MAC	(MI_TDES_CBC_PAD | _CBC_MAC_)
///////////////////

#define _CTS_MODE_				(0x04)  
#define MI_IS_CTS_MODE(m) 		((m) & _CTS_MODE_)

#define MI_AES_CBC_CTS		(MI_AES_CBC | _CTS_MODE_)
#define MI_AES_CBC_CTS_MAC	(MI_AES_CBC_CTS | _CBC_MAC_)

//#define KEYID_SECKEY256 1
#define KEYID_SECKEY128 1

#define MAX_KEY_LEN	(64)
#define MAX_IV_LEN	(32)

#ifndef __ASSEMBLY__
#ifdef  __cplusplus
extern "C" {
#endif

struct crypt_info {
	unsigned int		mode;
	unsigned int		keytype;
	unsigned int		keylen;
	unsigned char	key[MAX_KEY_LEN];
	unsigned int		ivlen;
	unsigned char	iv[MAX_IV_LEN];
	unsigned int		data_len;
};

struct crypt_oper {
	unsigned char	*src_addr; 
	unsigned char	*dst_addr; 
	unsigned int		src_len; 
	unsigned int		*dst_len; 
	int				final; 
};

#ifdef __cplusplus
}
#endif
#endif
#endif /*!__ASSEMBLY__*/
#endif	/* _CRYPTO_S5P_ACE_H */
