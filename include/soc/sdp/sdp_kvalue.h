
#ifndef __SDP_KVALUE_H__
#define __SDP_KVALUE_H__

#include <linux/types.h>

int sdp_kvalue_setptr_early(const char *key, void *val);

int sdp_kvalue_setu32(const char *key, u32 val);

int sdp_kvalue_getu32(const char *key, u32 *val);

int sdp_kvalue_setptr(const char *key, void *val);

int sdp_kvalue_getptr(const char *key, void **val);

int sdp_kvalue_setaction(const char *key, int (*action)(void *arg, void *val), void *arg);

#endif

