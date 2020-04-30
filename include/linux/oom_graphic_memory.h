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
enum {
	GEM_GRAPHIC_TYPE = 1,
	MALI_GRAPHIC_TYPE = 2,
	MAX_GRAPHIC_TYPE,
};

extern int graphic_memory_func_register(int type, void *func);
extern int graphic_memory_func_unregister(int type);

