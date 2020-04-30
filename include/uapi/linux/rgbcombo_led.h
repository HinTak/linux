/*
 *
 * Copyright (c) 2018 Samsung Electronics Co. Ltd.
 * Copyright (c) 2018 Samsung R&D Institute.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 */

#ifndef __RGBCOMBO_LED_H__
#define __RGBCOMBO_LED_H__

#include<asm-generic/ioctl.h>

#define RGBCOMBO_LED_MAX_SEQUENCE	10
#define RGBCOMBO_SEQUNCE_INFINITY_LOOP	0xFF

enum rgbcombo_leds {
	RGBCOMBO_LED_INDEX_0 = 0,
	RGBCOMBO_LED_INDEX_1,
	RGBCOMBO_LED_INDEX_2,
	RGBCOMBO_LED_INDEX_3,	/* reserved ....*/
	RGBCOMBO_LED_INDEX_4,	/* reserved ....*/

	RGBCOMBO_LED_INDEX_NUM,
};

enum rgbcombo_color {
	RGBCOMBO_LED_COLOR_RED,
	RGBCOMBO_LED_COLOR_GREEN,
	RGBCOMBO_LED_COLOR_BLUE,
	RGBCOMBO_LED_COLOR_PURPLE,
	RGBCOMBO_LED_COLOR_YELLOW,
	RGBCOMBO_LED_COLOR_CYAN,
	RGBCOMBO_LED_COLOR_WHITE,
	RGBCOMBO_LED_COLOR_NUM_MAX,
};

/**
 * struct rgbcombo_led_info - information for led.
 * @property : specifies just to notify led's property to user. it is optional and user
 *      can know the led's property with this.
 *         bit[2~0] : mean led types, 0:GPIO, 1:RGBC ....
 *         bit[3] : mean that user can use this led or not. 0: can't use, 1: use.
 * @color : specifies color of led,
 *        if the led is can not support color setting, it will be ignored in driver level.
 * @on: specifies on status. if value is 0, mean the off status otherwise mean the on status.
 */
#define RGBCOMBO_LED_PROP_TYPE_MASK(property)		((property)  & 0x07)
#define RGBCOMBO_LED_PROP_USE_MASK(property)		(((property) & 0x08) >> 3)

struct rgbcombo_led_info {
	unsigned char property;	/* do not touch this, it just provide by driver. */
	unsigned char color;
	unsigned char on;
};

/**
 * struct rgbcombo_led_control - information for led.
 * @leds : specifies infomations of leds.
 * @delay_time: specifies on status.
 *     if value is 0, it is not affect otherwise after delay time(msec), next sequence will be excuted
 *     when using RGBCOMBO_LED_IOCTL_SEQUENCE_CONTROL ioctl.
 */
struct rgbcombo_led_control {
	struct rgbcombo_led_info leds[RGBCOMBO_LED_INDEX_NUM];
	unsigned int delay_time;
};

/**
 * struct rgbcombo_led_sequence_data - infomation for the sequencial led control.
 * @num_sequence : specifies number of sequence.
 * @num_loop: specifies using repetition or not.
 *     if value is 0xFF, it mean infinity loop. otherwise looping num_loop time.
 * @sequences: specifies array of sequences.
 *
 * Rgbcombo_led driver will control the each leds accoding to information
 * when delay time of each sequences is expired.
 */
struct rgbcombo_led_sequence_data {
	unsigned char num_sequence;
	unsigned char num_loop;
	struct rgbcombo_led_control sequences[RGBCOMBO_LED_MAX_SEQUENCE];
};

/* IOCTL base */
#define RGBCOMBO_LED_IOCTL_IOCBASE		0xB3

/* IOCTL list for rgbcombo led driver */
#define RGBCOMBO_LED_IOCTL_GET_STATUS		_IOR(RGBCOMBO_LED_IOCTL_IOCBASE, 0x00, \
						struct rgbcombo_led_control)

#define RGBCOMBO_LED_IOCTL_SEQUENCE_CONTROL	_IOWR(RGBCOMBO_LED_IOCTL_IOCBASE, 0x01, \
						struct rgbcombo_led_sequence_data)

#endif	/*__RGBCOMBO_LED_H__*/
