#ifndef _UEP_CONFIG_H_
#define _UEP_CONFIG_H_

/*
#define SFD_UEP_SIGN_MAG0   0x1A
#define SFD_UEP_SIGN_MAG1   0xD4
#define SFD_UEP_SIGN_MAG2   0x77
#define SFD_UEP_SIGN_MAG3   0x5B

*/

#define SFD_UEP_SIGN_MAG0   ':'
#define SFD_UEP_SIGN_MAG1   'U'
#define SFD_UEP_SIGN_MAG2   'E'
#define SFD_UEP_SIGN_MAG3   'P'



#if defined(CONFIG_KERNEL_MODE_NEON) && defined(CONFIG_CRYPTO_SHA2_ARM_CE)
#define HASH_ALGO_NAME   "sha256-ce"
#define HASH_ALGO_LENGTH 32
#define KUEP_HASH_SHA256
#elif defined(CONFIG_KERNEL_MODE_NEON) && defined(CONFIG_CRYPTO_SHA256_ARM)
#define HASH_ALGO_NAME   "sha256-neon"
#define HASH_ALGO_LENGTH 32
#define KUEP_HASH_SHA256
#elif defined(CONFIG_CRYPTO_SHA256_ARM)
#define HASH_ALGO_NAME   "sha256-asm"
#define HASH_ALGO_LENGTH 32
#define KUEP_HASH_SHA256
#elif defined(CONFIG_CRYPTO_SHA256)
#define HASH_ALGO_NAME   "sha256"
#define HASH_ALGO_LENGTH 32
#define KUEP_HASH_SHA256
#else
// #define HASH_ALGO_NAME   "md5"
// #define HASH_ALGO_LENGTH 16
#error "Kernel has to support sha256 for kUEP since 2016 year"
#endif 


#if defined(KUEP_HASH_SHA256)
// sha256 DER 
static const unsigned char RSA_der_enc [] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
        0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
        0x04, 0x02, 0x01, 0x05, 0x00, 0x04,
        0x20};
#else
// MD5
static const Uint8 RSA_der_enc [] = { 0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                          0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10 };

#endif



#endif  /* _UEP_CONFIG_H_ */