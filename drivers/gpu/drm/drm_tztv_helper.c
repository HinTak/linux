/* drm_tztv_helper.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Authors:
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_tztv_helper.h>

/* Below Enum values should be matched to drm_tztv.h */

/* DRM IOCTL to check for TV (refer drm_tztv.h)*/
#define DRM_IOCTL_TZTV_SET_ONOFF   0x15

/* Plane Mute Sync HW command (refer drm_tztv.h)*/
enum drm_tztv_onoff_cmd {
	DRM_TZTV_CMD_SYNC_ONOFF,               /* TV SYNC param Ioctls command */
	DRM_TZTV_CMD_MUTE_ONOFF,               /* TV MUTE param Ioctls command */
	DRM_TZTV_CMD_MAX
};

/* Drm TZTV Helper property values */
enum drm_tztv_force_mute_sync {
	DRM_TZTV_PLANE_FORCE_INVALID,
	DRM_TZTV_PLANE_FORCE_MUTE_ON_SYNC_OFF,
	DRM_TZTV_PLANE_FORCE_MUTE_OFF_SYNC_ON,
	DRM_TZTV_PLANE_FORCE_RESTORE,
};

/* Tizen DRM Driver HW Control Command refer (drm_tztv.h) */
struct drm_tztv_onoff_param {
	unsigned int plane_id;
	enum drm_tztv_onoff_cmd cmd;
	unsigned int val;
};

static char *drm_prop_force_val_to_str(int val)
{
	switch (val) {
	case DRM_TZTV_PLANE_FORCE_MUTE_ON_SYNC_OFF:
		return "force mute on sync off";
	case DRM_TZTV_PLANE_FORCE_MUTE_OFF_SYNC_ON:
		return "force mute off sync on";
	case DRM_TZTV_PLANE_FORCE_RESTORE:
		return "force restore mute sync";
	default:
		return "unknown";
	}
}

/* PLANE MUTE SYNC STATUS 
	MUTE SYNC  VAL
	0    0     0
	0    1     1
	1    0     2
	1    1     3
*/
static inline uint64_t set_sync(unsigned int mute_sync, int sync) {

	if (mute_sync == DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF && sync == 1)
		return DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON && sync == 0)
		return DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF && sync == 1)
		return DRM_TZTV_PLANE_MUTE_ON_SYNC_ON;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_ON_SYNC_ON && sync == 0)
		return DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF;
	else
		return mute_sync;
}
static inline uint64_t set_mute(unsigned int mute_sync, int mute) {

	if (mute_sync == DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF && mute == 1)
		return DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF && mute == 0)
		return DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON && mute == 1)
		return DRM_TZTV_PLANE_MUTE_ON_SYNC_ON;
	else if(mute_sync == DRM_TZTV_PLANE_MUTE_ON_SYNC_ON && mute == 0)
		return DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON;
	else
		return mute_sync;
}

 /* drm_tztv_ioctl_hook -  DRM Ioctl hook Function to check Driver Specific
 *						IOCTLs command and process it
 *
 * @nr: ioctls command number
 * @dev: DRM device
 * @data: ioctl data*
 * @file_priv: DRM file info
 * @data: data pointer for the ioctl
 *
 * Checks for TV specific ioctl to check & handle drm prop specific
 * like MUTE/SYNC
 * Returns:
 * Zero Ignore device ioctl call ( do not call device driver function)
 * Non Zero values : call device ioctl( pass to device driver)
 */
int drm_tztv_ioctl_hook(unsigned int nr, struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	int ret = -EINVAL;
	struct drm_tztv_onoff_param *args;
	struct drm_mode_config *conf = &dev->mode_config;
	struct drm_mode_object *obj;
	struct drm_plane *plane;
	int is_mute_sync_cmd = 0;
	uint64_t mute_sync = 0;
	uint64_t val = 0;
	uint64_t force_mute_sync = 0;

	/* Check Device Driver IOCTL for MUTE SYNC Command */

	if (DRM_IOCTL_TZTV_SET_ONOFF == nr - DRM_COMMAND_BASE) {
		args = data;
		/* Only Check for Mute/ Syn Commabd  now */
		if (args->cmd == DRM_TZTV_CMD_SYNC_ONOFF || args->cmd == DRM_TZTV_CMD_MUTE_ONOFF) {

			mutex_lock(&dev->mode_config.mutex);
			obj = drm_mode_object_find(dev, args->plane_id, DRM_MODE_OBJECT_PLANE);
			if (!obj) {
				mutex_unlock(&dev->mode_config.mutex);
				goto out;
			}

			plane = obj_to_plane(obj);

			drm_object_property_get_value(&plane->base, conf->tztv_plane_mute_sync, &mute_sync);

			switch (args->cmd) {
			case DRM_TZTV_CMD_SYNC_ONOFF:
				val = set_sync(mute_sync, args->val);	
				drm_object_property_set_value(&plane->base, conf->tztv_plane_mute_sync, val);
				is_mute_sync_cmd = 1;
				break;
			case DRM_TZTV_CMD_MUTE_ONOFF:
				val = set_mute(mute_sync, args->val);	
				drm_object_property_set_value(&plane->base, conf->tztv_plane_mute_sync, val);
				is_mute_sync_cmd = 1;
				break;
			default:
				is_mute_sync_cmd = 0;
				break;
			}

			if (is_mute_sync_cmd == 1) {	
				drm_object_property_get_value(&plane->base, conf->tztv_force_mute_sync, &force_mute_sync);

				if (force_mute_sync == DRM_TZTV_PLANE_FORCE_MUTE_OFF_SYNC_ON ||
					force_mute_sync == DRM_TZTV_PLANE_FORCE_MUTE_ON_SYNC_OFF) {
					printk(KERN_INFO "DRM Plane [%s] ignore HW Setting !!![%d:%s]",
								drm_prop_force_val_to_str(force_mute_sync),
								current->pid, current->comm);
					ret = 0;
				}
			}
			mutex_unlock(&dev->mode_config.mutex);
		}
	}
out:
	return ret;
}

/**
 * drm_tztv_set_plane_hw_mute_sync - set tv specifc standard  value of a property
 * @plane: drm plane object to set property value for
 * @property: property to set
 * @value: value the property should be set to
 *
 * This functions sets a given property on a given plane object..
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_tztv_set_plane_hw_mute_sync(struct drm_plane *plane,
				int cmd, uint64_t value)
{
	int ret = -EINVAL;
	struct drm_device *dev = plane->dev;
	const struct drm_ioctl_desc *ioctl = NULL;
	drm_ioctl_t *func;
	struct drm_tztv_onoff_param arg;

	ioctl = &dev->driver->ioctls[DRM_IOCTL_TZTV_SET_ONOFF];

	func = ioctl->func;
	if (unlikely(!func)) {
		DRM_DEBUG("tztv : no function\n");
		ret = -EINVAL;
		goto err;
	}
	arg.plane_id = plane->base.id;
	arg.cmd = cmd;
	arg.val = value;
	/* Call Device Driver Function to HW Setting */
	ret = func(dev, &arg, NULL);
err:
	return ret;
}
/**
 * drm_tzv_plane_set_obj_prop - set tv specifc standard  value of a property
 * @plane: drm plane object to set property value for
 * @property: property to set
 * @value: value the property should be set to
 *
 * This functions sets a given property on a given plane object..
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_tztv_plane_set_obj_prop(struct drm_plane *plane,
				struct drm_property *property,
				uint64_t value)
{
	int ret = -EINVAL;
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *conf = &dev->mode_config;
	struct drm_mode_object *obj = &plane->base;
	unsigned int val = value;

	if (property == conf->tztv_force_mute_sync) {
		/* set mute/sync for plane */
		drm_object_property_set_value(obj, property, value);
		printk(KERN_INFO "DRM Plane set %s done [%d:%s] !!!", drm_prop_force_val_to_str(val), current->pid, current->comm);
		ret = 0;
	}
	return ret;
}
/**
*drm_tztv_prop_set_ioctl - set tv plane mute sync ioctl
* @plane: drm plane object to set property value for
* @property: property
* @value: force mute sync command
*
* This functions Calls the mute/ sync ioctl to set HW state
* based on the user value
*
* Returns:
*/
void drm_tztv_prop_mute_sync_set_to_chipdriver(struct drm_plane *plane,
			struct drm_property *property, unsigned int value)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *conf = &dev->mode_config;
	uint64_t prop_val = 0;
	unsigned int mute = 0;
	unsigned int sync = 0;

	if (property == conf->tztv_force_mute_sync) {
		switch (value) {
		case DRM_TZTV_PLANE_FORCE_MUTE_OFF_SYNC_ON:
		{
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_MUTE_ONOFF, 0);
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_SYNC_ONOFF, 1);
			break;
		}
		case DRM_TZTV_PLANE_FORCE_MUTE_ON_SYNC_OFF:
		{
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_MUTE_ONOFF, 1);
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_SYNC_ONOFF, 0);
			break;
		}
		case DRM_TZTV_PLANE_FORCE_RESTORE:
		{
			/* Set MUTE to apps values set */
			drm_object_property_get_value(&plane->base, conf->tztv_plane_mute_sync, &prop_val);
			if(prop_val == DRM_TZTV_PLANE_MUTE_OFF_SYNC_OFF) {
				mute = 0;
				sync = 0;
			} else if (prop_val == DRM_TZTV_PLANE_MUTE_ON_SYNC_OFF) {
				mute = 1;
				sync = 0;
			} else if (prop_val == DRM_TZTV_PLANE_MUTE_OFF_SYNC_ON) {
				mute = 0;
				sync = 1;
			} else if (prop_val == DRM_TZTV_PLANE_MUTE_ON_SYNC_ON) {
				mute = 1;
				sync = 1;
			}
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_MUTE_ONOFF, mute);
			drm_tztv_set_plane_hw_mute_sync(plane, DRM_TZTV_CMD_SYNC_ONOFF, sync);
			break;
		}
		default:
			break;
		}
	}
}
