/* include/linux/logger.h
 *
 * Copyright (C) 2017-2018 Samsung, Inc.
 * Author: Jusun Song <jsun.song@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_CMA_LOG_TRACE_H
#define _LINUX_CMA_LOG_TRACE_H
#include <linux/page_ref.h>

struct log_trace_data{
    unsigned int size;
       unsigned int pfn;
       unsigned long *to;
};

#define REFTRACE_META_SIZE 2
#define REFTRACE_BACKTRACE_NR REFTRACE_BT_ENTRIES
#define REFTRACE_ENTRIES_SIZE (REFTRACE_BT_ENTRIES + REFTRACE_META_SIZE)
#define REFCOUNT_SHIFT 21
#define INCREARE_SHIFT 31

#define ENTRY_TO_PFN(entry) (0x1fffff & entry) 
#define ENTRY_TO_REFCOUNT(entry) ((0x7FE00000 & entry) >> REFCOUNT_SHIFT) 
#define ENTRY_TO_TYPE(entry) (entry >> INCREARE_SHIFT) 


#define DEFAULT_LOG_SZ         524250                                                                  /* 2MB = 2097152, 2097152 / 4byte = 524288 ~= 524250 */
#define GLOBAL_ENTRIES         (524250 / REFTRACE_ENTRIES_SIZE)

#if (REFTRACE_ENTRIES_SIZE == 3)
#define PER_CPU_TRACE_ENTRIES 42000    /* 42000 *  3 = 126000 */
#define CPU_PER_BUF_SZ (REFTRACE_ENTRIES_SIZE * PER_CPU_TRACE_ENTRIES)  /* 512KB = 524288, 524288 / 4byte = 131072 entry,  42000 * 3 = 126000 */

#elif (REFTRACE_ENTRIES_SIZE == 7)
#define PER_CPU_TRACE_ENTRIES 26000    /* 26000 *  5 = 130000 */
#define CPU_PER_BUF_SZ (REFTRACE_ENTRIES_SIZE * PER_CPU_TRACE_ENTRIES)  /* 512KB = 524288, 524288 / 4byte = 131072 entry,  26000 * 5 = 130000 */
#endif

#endif /* _LINUX_CMA_LOG_TRACE_H */

