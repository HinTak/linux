/* drm_tv_helper.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Authors:
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __DRM_TZTV_HELPER_H__
#define __DRM_TZTV_HELPER_H__

/* Define to enable tztv_helper  */
#include <drm/drm_crtc.h>

/* PLANE MUTE SYNC STATUS
        MUTE SYNC  VAL
        0    0     0
        0    1     1
        1    0     2
        1    1     3
*/
enum drm_tztv_plane_mute_sync {
	DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF,
	DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON,
	DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF,
	DRM_TZTV_PLANE_MUTE_ON_SYNC_ON,
};


extern int drm_tztv_ioctl_hook(unsigned int nr, struct drm_device *dev, void *data,
                      struct drm_file *file_priv);

extern int drm_tztv_plane_set_obj_prop(struct drm_plane *plane,
                                struct drm_property *property,
                                uint64_t value);

extern void drm_tztv_prop_mute_sync_set_to_chipdriver(struct drm_plane *plane,
							struct drm_property *property, 
							unsigned int value);
							
#endif /*__DRM_TZTV_HELPER_H__*/
