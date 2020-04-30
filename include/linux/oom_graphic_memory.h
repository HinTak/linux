/*
*  Copyright (C) 2016-2017 Samsung Electronics Co., Ltd.
*
*  This program is free software; you can redistribute it and/or
*  modify it under the terms of the GNU General Public License
*  as published by the Free Software Foundation; either version 2
*  of the License, or (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*/

/* More graphic type can be added */

#ifndef __OOM_GRAPHICS_H__
#define __OOM_GRAPHICS_H__

enum {
	GEM_GRAPHIC_TYPE = 1,
	MALI_GRAPHIC_TYPE = 2,
	GPU_GRAPHIC_TYPE = 2,
	MAX_GRAPHIC_TYPE,
};

struct img_driver_stats
{
	unsigned int ui32MemoryUsageKMalloc;
	unsigned int ui32MemoryUsageKMallocMax;
	unsigned int ui32MemoryUsageVMalloc;
	unsigned int ui32MemoryUsageVMallocMax;
	unsigned int ui32MemoryUsageAllocPTMemoryUMA;
	unsigned int ui32MemoryUsageAllocPTMemoryUMAMax;
	unsigned int ui32MemoryUsageVMapPTUMA;
	unsigned int ui32MemoryUsageVMapPTUMAMax;
	unsigned int ui32MemoryUsageAllocPTMemoryLMA;
	unsigned int ui32MemoryUsageAllocPTMemoryLMAMax;
	unsigned int ui32MemoryUsageIORemapPTLMA;
	unsigned int ui32MemoryUsageIORemapPTLMAMax;
	unsigned int ui32MemoryUsageAllocGPUMemLMA;
	unsigned int ui32MemoryUsageAllocGPUMemLMAMax;
	unsigned int ui32MemoryUsageAllocGPUMemUMA;
	unsigned int ui32MemoryUsageAllocGPUMemUMAMax;
	unsigned int ui32MemoryUsageAllocGPUMemUMAPool;
	unsigned int ui32MemoryUsageAllocGPUMemUMAPoolMax;
	unsigned int ui32MemoryUsageMappedGPUMemUMA_LMA;
	unsigned int ui32MemoryUsageMappedGPUMemUMA_LMAMax;
	unsigned int reserved1;
	unsigned int reserved2;
	unsigned int reserved3;
	unsigned int reserved4;

};

extern int graphic_memory_func_register(int type, void *func);
extern int graphic_memory_func_unregister(int type);
extern int graphic_img_driver_stats_register(void *func);

#endif
