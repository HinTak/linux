/*
 *  Capabilities Linux Security Module
 *
 *  This is the default security module in case no other module is loaded.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/security.h>

static int cap_bprm_check_security(struct linux_binprm *bprm)
{
	return 0;
}

#define set_to_cap_if_null(ops, function)				\
	do {								\
		if (!ops->function) {					\
			ops->function = cap_##function;			\
			pr_debug("Had to override the " #function	\
				 " security operation with the default.\n");\
			}						\
	} while (0)

void __init security_fixup_ops(struct security_operations *ops)
{
	set_to_cap_if_null(ops, bprm_check_security);
}
