/*
 * PWM based RGB LED control
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/rgbcombo_led.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

/* Defines */
#define RGBCOMBO_LED_NAME		"misc_led"
#define RGBCOMBO_LED_MINOR		193

#define LED_STATE_ON	1
#define LED_STATE_OFF	0

#define NUM_MAX_PWM_RGBC_LED_COLOR	RGBCOMBO_LED_COLOR_NUM_MAX
#define NUM_MAX_LEDS		RGBCOMBO_LED_INDEX_NUM
#define NUM_MAX_GPIO_LED	NUM_MAX_LEDS
#define NUM_MAX_RGBC_LED	1

#define RGBCOMBO_LED_PROP_USE			(1<<3)

#define USE_OWN_WORK_QUEUE	1

enum rgbcombo_led_type {
	GPIO_LED_TYPE,
	RGBC_LED_TYPE,
	PWM_LED_TYPE,
	NUM_LED_TYPE,
};

/* For GPIO Leds */
struct gpio_led {
	unsigned int gpio;
	unsigned char	default_state;
};

/* For pwm Leds */
struct pwm_info {
	int	period;
	int	duty;
};

struct pwm_data {
	struct pwm_device	*pwm;
	struct pwm_info	info;
};

/* For Pwm RGBC Leds */
enum pwm_rgbc_leds {
	PWM_RGBC_LED_RED,
	PWM_RGBC_LED_GREEN,
	PWM_RGBC_LED_BLUE,
	PWM_RGBC_LED_NUM,
};

static const char *pwm_rgbc_led_names[PWM_RGBC_LED_NUM] = {
	[PWM_RGBC_LED_RED] 		= "red",
	[PWM_RGBC_LED_GREEN]	= "green",
	[PWM_RGBC_LED_BLUE]		= "blue",
};

struct pwm_rgbc_led {
	struct pwm_data color_pwms[PWM_RGBC_LED_NUM];
	struct pwm_info *color_tables[PWM_RGBC_LED_NUM];
	const char *color_names[NUM_MAX_PWM_RGBC_LED_COLOR];
	unsigned char index;
	unsigned char	color;
	unsigned char	new_color;
	unsigned char	default_color;
	unsigned char	num_color;

	unsigned char	default_state;
};

/* Structure for driver data */
struct rgbcombo_data {
	struct device *dev;
	struct miscdevice miscdev;
	struct mutex lock;
	unsigned char num_leds;
	unsigned char num_gpio_leds;
	unsigned char num_rgbc_leds;
	enum rgbcombo_led_type led_types[NUM_MAX_LEDS];
	struct rgbcombo_led_control current_state;

	/* For Sequence infomation*/
#if USE_OWN_WORK_QUEUE
	struct workqueue_struct *work_queue;
#endif
	struct delayed_work sequence_work;
	struct rgbcombo_led_control sequences[RGBCOMBO_LED_MAX_SEQUENCE];
	unsigned char num_sequence;
	unsigned char num_loop;
	unsigned char current_sequence;

	/* For debug */
	char *trace_buffer;
	unsigned char debug_enable;

	/* For LED driven by PWM */
	struct pwm_rgbc_led rgbc;
	/* For LED driven by GPIO */
	struct gpio_led gpios[NUM_MAX_GPIO_LED];
};

#define TRACE_TMP_BUFFER_SIZE	PAGE_SIZE

#define SEQUENCE_DBG(enable, dev, fmt, arg...)	\
do {										\
	if (enable) {			\
		dev_err(dev, fmt, ##arg);			\
	}									\
} while (0)

static void __rgbcombo_gpio_led_set(struct rgbcombo_data *led_data, unsigned char led_idx, unsigned char cur_seq)
{
	struct gpio_led *gpio = &led_data->gpios[led_idx];
	struct rgbcombo_led_control *sequence = &led_data->sequences[cur_seq];
	unsigned char new_level = sequence->leds[led_idx].on;

	if (gpio_is_valid(gpio->gpio))
		gpio_set_value(gpio->gpio, new_level ? LED_STATE_ON : LED_STATE_OFF);

	led_data->current_state.leds[led_idx].on = new_level ? LED_STATE_ON : LED_STATE_OFF;
}

static void __rgbcombo_rgbc_led_set(struct rgbcombo_data *led_data, unsigned char led_idx, unsigned char cur_seq)
{
	struct pwm_rgbc_led *rgbc = &led_data->rgbc;
	struct rgbcombo_led_control *sequence = &led_data->sequences[cur_seq];
	unsigned char new_color = sequence->leds[led_idx].color;
	unsigned char new_level = sequence->leds[led_idx].on;

	if (rgbc->color != new_color) {
		pwm_config(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm,
			rgbc->color_tables[PWM_RGBC_LED_RED][new_color].duty,
			rgbc->color_tables[PWM_RGBC_LED_RED][new_color].period);
		pwm_config(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm,
			rgbc->color_tables[PWM_RGBC_LED_GREEN][new_color].duty,
			rgbc->color_tables[PWM_RGBC_LED_GREEN][new_color].period);
		pwm_config(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm,
			rgbc->color_tables[PWM_RGBC_LED_BLUE][new_color].duty,
			rgbc->color_tables[PWM_RGBC_LED_BLUE][new_color].period);
		rgbc->color = new_color;
	}

	if (new_level == LED_STATE_OFF) {
		pwm_disable(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm);
		pwm_disable(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm);
		pwm_disable(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm);
	} else {
		pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm);
		pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm);
		pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm);
	}

	led_data->current_state.leds[led_idx].color = new_color;
	led_data->current_state.leds[led_idx].on = new_level ? LED_STATE_ON : LED_STATE_OFF;
}

static int __rgbcombo_led_set(struct rgbcombo_data *led_data, unsigned char led_idx, unsigned char cur_seq)
{
	enum rgbcombo_led_type led_type = led_data->led_types[led_idx];

	switch(led_type) {
		/* Control GPIO leds */
		case GPIO_LED_TYPE:
			__rgbcombo_gpio_led_set(led_data, led_idx, cur_seq);
			break;
		/* Control PWM leds */
		case RGBC_LED_TYPE:
			__rgbcombo_rgbc_led_set(led_data, led_idx, cur_seq);
			break;
		case PWM_LED_TYPE:
			dev_err(led_data->dev, "Not support currentely PWM_LED_TYPE LED %d\n", led_idx);
			break;
		default:
			dev_err(led_data->dev, "unable to request %d index LED\n", led_idx);
			break;
	}

	return 0;
}

static void __rgbcombo_led_set_all(struct rgbcombo_data *led_data)
{
	unsigned char led_idx;
	unsigned char cur_seq_idx = led_data->current_sequence;

	/* If final sequence check loop is remained or not */
	if (cur_seq_idx >= led_data->num_sequence) {
		/* reset current_sequence */
		cur_seq_idx = 0;

		if (led_data->num_loop == RGBCOMBO_SEQUNCE_INFINITY_LOOP) {
			SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "[SEQ] Reach to final sequence. Restart by Infinity loop");
		} else {
			if (led_data->num_loop >= 1) {
				led_data->num_loop--;
				SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "[SEQ] Reach to final sequence. Restart by loop[remain(%d)]\n", led_data->num_loop);
			}
			if (led_data->num_loop == 0) {
				cancel_delayed_work(&led_data->sequence_work);
				SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "[SEQ] Reach to final loop. Finish sequence.\n");
				led_data->current_sequence = cur_seq_idx;
				return;
			}
		}
	}

	/* Excute current sequence */
	for (led_idx = 0; led_idx < led_data->num_leds; led_idx++) {
		if (__rgbcombo_led_set(led_data, led_idx, cur_seq_idx) < 0) {
			dev_err(led_data->dev, "Fail to set %d index led\n", led_idx);
			continue;
		}
	}
	SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "[SEQ] Excuted sequence [%d/%d]\n",
						cur_seq_idx, led_data->num_sequence);

	/* put the next sequence into delayed work */
	if (cur_seq_idx < led_data->num_sequence) {
		cancel_delayed_work(&led_data->sequence_work);

		if (led_data->sequences[cur_seq_idx].delay_time) {
			SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "[SEQ] Will be excuted next sequence [%d/%d] after %d msec.\n",
						cur_seq_idx + 1, led_data->num_sequence,
						led_data->sequences[cur_seq_idx].delay_time);

			/* increase for next sequence */
			led_data->current_sequence = cur_seq_idx + 1;

#if USE_OWN_WORK_QUEUE
			queue_delayed_work(led_data->work_queue, &led_data->sequence_work,
					msecs_to_jiffies(led_data->sequences[cur_seq_idx].delay_time));
#else
			schedule_delayed_work(&led_data->sequence_work,
					msecs_to_jiffies(led_data->sequences[cur_seq_idx].delay_time));
#endif
		}
	}
}

static void rgbcombo_led_set_sequence_work(struct work_struct *work)
{
	struct rgbcombo_data *led_data =
		container_of(work, struct rgbcombo_data, sequence_work.work);

	__rgbcombo_led_set_all(led_data);
}

static void rgbcombo_led_set_default_state(struct rgbcombo_data *led_data)
{
	unsigned char led_idx;
	enum rgbcombo_led_type led_type;

	for (led_idx = 0; led_idx < led_data->num_leds; led_idx++) {
		led_type = led_data->led_types[led_idx];

		switch(led_type) {
			/* Control GPIO leds */
			case GPIO_LED_TYPE:
			{
				struct gpio_led *gpio = &led_data->gpios[led_idx];

				/* set default current state */
				led_data->current_state.leds[led_idx].property = RGBCOMBO_LED_PROP_USE | GPIO_LED_TYPE;
				led_data->current_state.leds[led_idx].on = LED_STATE_OFF;

				dev_info(led_data->dev, "%d index Led found[type:%s, num:%d/%d]\n",
					led_idx, "GPIO", led_data->num_gpio_leds, led_data->num_leds);

				if (gpio->default_state != LED_STATE_ON)
					break;

				if (gpio_is_valid(gpio->gpio))
					gpio_set_value(gpio->gpio, LED_STATE_ON);

				led_data->current_state.leds[led_idx].on = LED_STATE_ON;

				dev_info(led_data->dev, "Set default %d GPIO Led is %s.\n",
							led_idx, gpio->default_state == LED_STATE_ON ? "On" : "Off");
			}
				break;
			/* Control PWM leds */
			case RGBC_LED_TYPE:
			{
				struct pwm_rgbc_led *rgbc = &led_data->rgbc;
				unsigned char new_color = 0;

				/* set default current state */
				led_data->current_state.leds[led_idx].property = RGBCOMBO_LED_PROP_USE | RGBC_LED_TYPE;
				led_data->current_state.leds[led_idx].on = LED_STATE_OFF;

				dev_info(led_data->dev, "%d index Led found[type:%s, num:%d/%d]\n",
					led_idx, "RGBC", led_data->num_rgbc_leds, led_data->num_leds);

				if (rgbc->default_state != LED_STATE_ON)
					break;

				new_color = rgbc->default_color;
				pwm_config(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm,
					rgbc->color_tables[PWM_RGBC_LED_RED][new_color].duty,
					rgbc->color_tables[PWM_RGBC_LED_RED][new_color].period);
				pwm_config(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm,
					rgbc->color_tables[PWM_RGBC_LED_GREEN][new_color].duty,
					rgbc->color_tables[PWM_RGBC_LED_GREEN][new_color].period);
				pwm_config(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm,
					rgbc->color_tables[PWM_RGBC_LED_BLUE][new_color].duty,
					rgbc->color_tables[PWM_RGBC_LED_BLUE][new_color].period);
				rgbc->color = new_color;

				pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm);
				pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm);
				pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm);

				/* set current state */
				led_data->current_state.leds[led_idx].color = rgbc->color;
				led_data->current_state.leds[led_idx].on = LED_STATE_ON;

				dev_info(led_data->dev, "Set default RGBC Led : %s is %s.\n",
							rgbc->color_names[new_color],
							rgbc->default_state == LED_STATE_ON ? "On" : "Off");
			}
				break;
			case PWM_LED_TYPE:
				dev_err(led_data->dev, "Not support currentely PWM_LED_TYPE LED %d\n", led_idx);
				break;
			default:
				dev_err(led_data->dev, "unable to request %d index LED\n", led_idx);
				break;
		}
	}
}

static int rgbcombo_led_pwm_add(struct rgbcombo_data *led_data, struct device_node *np, int index)
{
	struct device *dev = led_data->dev;
	struct pwm_rgbc_led *rgbc = &led_data->rgbc;
	struct pwm_data  *data= &rgbc->color_pwms[index];
	int ret = 0;


	data->pwm = devm_of_pwm_get(dev, np, NULL);

	if (IS_ERR(data->pwm)) {
		ret = PTR_ERR(data->pwm);
		dev_err(dev, "unable to request PWM for %s: %d\n", pwm_rgbc_led_names[index], ret);
		return ret;
	}
	data->info.period = pwm_get_period(data->pwm);

	return ret;
}

static int rgbcombo_led_get_color_pwms(struct rgbcombo_data *led_data, struct device_node *np, int index)
{
	struct device *dev = led_data->dev;
	struct device_node *cnp;
	int ret = 0;

	cnp = of_get_child_by_name(np, pwm_rgbc_led_names[index]);
	if (!cnp) {
		dev_err(dev, "failed to find %s pwm node\n", pwm_rgbc_led_names[index]);
		return -ENODEV;
	}
	ret = rgbcombo_led_pwm_add(led_data, cnp, index);
	of_node_put(cnp);
	if (ret) {
		dev_err(dev, "Fail to add color_pwms\n");
		return ret;
	}

	return ret;
}

static int rgbcombo_led_get_rgbc_dt(struct rgbcombo_data *led_data)
{
	struct device *dev = led_data->dev;
	struct device_node *np = dev->of_node;
	struct device_node *cnp = NULL;

	const unsigned int *pidx = NULL;
	int idx =0, num_idx = 0;
	unsigned int value;
	const char *string = NULL;
	int ret = 0;

	cnp = of_get_child_by_name(np, "rgbc_led");

	if (!cnp) {
		dev_err(dev, "Failed to find rgbc_led node\n");
		return -ENODEV;
	}

	/* Get RGBC LED information */
	pidx = of_get_property(cnp, "color-idx", &num_idx);
	if (pidx && num_idx) {
		num_idx = num_idx / sizeof(u32);
		if (num_idx > NUM_MAX_PWM_RGBC_LED_COLOR) {
			dev_err(dev, "Color number[%d] is over than NUM_MAX_PWM_RGBC_LED_COLOR.\n", num_idx);
			ret = -EINVAL;
			goto out;
		}
	} else {
		dev_err(dev, "Failed to calculate number of colors.\n");
		ret = -EINVAL;
		goto out;
	}

	led_data->rgbc.num_color = num_idx;
	for (idx = PWM_RGBC_LED_RED; idx < PWM_RGBC_LED_NUM; idx++) {
		if (!led_data->rgbc.color_tables[idx]) {
			led_data->rgbc.color_tables[idx] = devm_kzalloc(dev, sizeof(struct pwm_info) * num_idx, GFP_KERNEL);
			if (!led_data->rgbc.color_tables[idx]) {
				dev_err(dev, "Failed to allocate memory of color_tables for %s LED\n", pwm_rgbc_led_names[idx]);
				ret = -ENOMEM;
				goto out;
			}
		}
	}

	/* Get and add frequncy and time table */
	for (idx = 0; idx < num_idx; idx++) {
		if (of_property_read_u32_index(cnp, "color-idx", idx, &value)) {
			dev_err(dev, "Failed to get %dth color-idx property\n", idx);
			ret = -EINVAL;
			goto out;
		}

		if(of_property_read_string_index(cnp, "color-names", idx, &led_data->rgbc.color_names[idx])) {
			dev_err(dev, "Failed to get %dth color-names property\n", idx);
			ret = -EINVAL;
			goto out;
		}

		ret = of_property_read_u32_index(cnp, "pwm-red,period", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_RED][value].period);
		ret |= of_property_read_u32_index(cnp, "pwm-red,duty", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_RED][value].duty);
		ret |= of_property_read_u32_index(cnp, "pwm-green,period", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_GREEN][value].period);
		ret |= of_property_read_u32_index(cnp, "pwm-green,duty", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_GREEN][value].duty);
		ret |= of_property_read_u32_index(cnp, "pwm-blue,period", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_BLUE][value].period);
		ret |= of_property_read_u32_index(cnp, "pwm-blue,duty", idx,
			 &led_data->rgbc.color_tables[PWM_RGBC_LED_BLUE][value].duty);
		if (ret) {
			dev_err(dev, "Failed to get data of %s color\n", led_data->rgbc.color_names[value]);
			ret = -EINVAL;
			goto out;
		}
	}

	/* Get RGB pwm  */
	for (idx = PWM_RGBC_LED_RED; idx < PWM_RGBC_LED_NUM; idx++) {
		ret = rgbcombo_led_get_color_pwms(led_data, cnp, idx);
		if (ret) {
			dev_err(dev, "failed to get %s pwm\n", pwm_rgbc_led_names[idx]);
			ret = -ENODEV;
			goto out;
		}
	}

	return 0;

out:
	of_node_put(cnp);
	return ret;
}

static int rgbcombo_led_get_gpio_dt(struct rgbcombo_data *led_data)
{
	struct device *dev = led_data->dev;
	struct device_node *np = dev->of_node;
	struct device_node *cnp = NULL;

	int idx =0;
	char name[10] = {0,};
	int ret = 0;

	cnp = of_get_child_by_name(np, "gpio_led");

	if (!cnp) {
		dev_err(dev, "Failed to find gpio_led node\n");
		return -ENODEV;
	}

	/* Get gpio information and set default status. */
	for (idx = 0; idx < led_data->num_leds; idx++) {
		if (led_data->led_types[idx] != GPIO_LED_TYPE)
			continue;

		memset(name, 0x00, sizeof(name));
		snprintf(name, sizeof(name), "gpio_led%d", idx);

		led_data->gpios[idx].gpio = of_get_named_gpio(cnp, name, 0);
		/* skip leds that aren't available */
		if (gpio_is_valid(led_data->gpios[idx].gpio)) {
			ret = devm_gpio_request_one(dev,led_data->gpios[idx].gpio,
					GPIOF_OUT_INIT_LOW, name);
			if (ret < 0) {
				dev_err(dev, "Unable to request gpio %d\n", led_data->gpios[idx].gpio);
				goto out;
			}
		} else {
			dev_err(dev, "%dth LED's Gpio is invaild\n", idx + 1);
			ret = -ENODEV;
			goto out;
		}
	}

	return 0;

out:
	of_node_put(cnp);
	return ret;
}

static int rgbcombo_led_parse_dt(struct rgbcombo_data *led_data)
{
	struct device *dev = led_data->dev;
	struct device_node *np = dev->of_node;

	const unsigned int *pidx = NULL;
	int idx =0, num_idx = 0;
	unsigned int value;
	const char *string = NULL;
	int ret = 0;

	/* Get number of LED */
	pidx = of_get_property(np, "led-idx", &num_idx);
	if (pidx && num_idx) {
		num_idx = num_idx / sizeof(u32);
		if (num_idx > NUM_MAX_LEDS) {
			dev_err(dev, "LED number[%d] is over than NUM_MAX_LEDS.\n", num_idx);
			return -EINVAL;
		}
	} else {
		dev_err(dev, "Failed to calculate number of LED.\n");
		return -EINVAL;
	}

	led_data->num_leds = num_idx;

	/* Get default status and type of LED */
	for (idx = 0; idx < num_idx; idx++) {
		if (of_property_read_u32_index(np, "led-idx", idx, &value)) {
			dev_err(dev, "Failed to get %dth led-idx property\n", idx);
			return -EINVAL;
		}
		if (of_property_read_u32_index(np, "led-types", idx, &led_data->led_types[idx])) {
			dev_err(dev, "Failed to get %dth led-types property\n", idx);
			return -EINVAL;
		}
		switch (led_data->led_types[idx]) {
			case GPIO_LED_TYPE:
				led_data->num_gpio_leds++;
				if (led_data->num_gpio_leds > led_data->num_leds) {
					dev_err(dev, "So many GPIO LED(%d) is requested\n", led_data->num_gpio_leds);
					return -EINVAL;
				}
				if(!of_property_read_string_index(np, "default-state", idx, &string)) {
					if (!strcmp(string, "on"))
						led_data->gpios[idx].default_state = LED_STATE_ON;
				}
				break;
			case RGBC_LED_TYPE:
				led_data->num_rgbc_leds++;
				/* Get each color's pwm data */
				if (led_data->num_rgbc_leds > NUM_MAX_RGBC_LED) {
					dev_err(dev, "So many RGBC LED(%d) is requested\n", led_data->num_rgbc_leds);
					return -EINVAL;
				}

				led_data->rgbc.index = idx;
				if(!of_property_read_string_index(np, "default-state", idx, &string)) {
					if (!strcmp(string, "on")) {
						led_data->rgbc.default_state = LED_STATE_ON;
						if (!of_property_read_u32_index(np, "default-color-idx", idx, &value))
							led_data->rgbc.default_color = value;
					}
				}
				break;
			default:
				break;
		}
	}

	ret = rgbcombo_led_get_rgbc_dt(led_data);
	if (ret) {
		dev_err(dev, "Failed to get rgbc led information(%d)\n", ret);
		return ret;
	}

	ret = rgbcombo_led_get_gpio_dt(led_data);
	if (ret) {
		dev_err(dev, "Failed to get gpio led information(%d)\n", ret);
		return ret;
	}

	return ret;
}

static ssize_t rgbcombo_led_rgbc_color_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", led_data->rgbc.color_names[led_data->rgbc.color]);
}

static ssize_t rgbcombo_led_rgbc_color_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);
	struct pwm_rgbc_led *rgbc = &led_data->rgbc;
	char color_name[10] = {0,};
	size_t len;
	int idx;

	color_name[sizeof(color_name) - 1] = '\0';
	strncpy(color_name, buf, sizeof(color_name) - 1);
	len = strlen(color_name);

	if (len && color_name[len - 1] == '\n')
		color_name[len - 1] = '\0';

	for (idx = 0; idx < rgbc->num_color; idx++) {
		if (!strcmp(color_name, rgbc->color_names[idx])) {
			dev_err(led_data->dev, "Set %s color\n", rgbc->color_names[idx]);
			break;
		}
	}

	if (idx == rgbc->num_color) {
		dev_err(led_data->dev, "Failed find %s color\n", color_name);
		goto out;
	}

	cancel_delayed_work_sync(&led_data->sequence_work);

	rgbc->color = idx;

	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm,
		rgbc->color_tables[PWM_RGBC_LED_RED][idx].duty,
		rgbc->color_tables[PWM_RGBC_LED_RED][idx].period);
	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm,
		rgbc->color_tables[PWM_RGBC_LED_GREEN][idx].duty,
		rgbc->color_tables[PWM_RGBC_LED_GREEN][idx].period);
	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm,
		rgbc->color_tables[PWM_RGBC_LED_BLUE][idx].duty,
		rgbc->color_tables[PWM_RGBC_LED_BLUE][idx].period);

	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm);
	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm);
	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm);

	led_data->current_state.leds[rgbc->index].color = rgbc->color;
	led_data->current_state.leds[rgbc->index].on = LED_STATE_ON;

out:
	return size;
}
static DEVICE_ATTR(color, S_IRUGO | S_IWUSR, rgbcombo_led_rgbc_color_show, rgbcombo_led_rgbc_color_store);

static ssize_t rgbcombo_led_rgbc_color_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);

	int i;
	int len = 0, led_idx = 0;

	if (!led_data->trace_buffer) {
		led_data->trace_buffer = devm_kzalloc(dev, TRACE_TMP_BUFFER_SIZE, GFP_KERNEL);
		if (!led_data->trace_buffer) {
			dev_err(dev, "Failed to allocate memory trace_tmp_buf.\n");
			return -ENOMEM;
		}
	}
	memset(led_data->trace_buffer, 0x00, TRACE_TMP_BUFFER_SIZE);

	for (i = 0; i < led_data->rgbc.num_color; i++) {
		len += snprintf(&led_data->trace_buffer[len], TRACE_TMP_BUFFER_SIZE - len,
							"%d:%s\t red[%d,%d]\t green[%d,%d]\t blue[%d,%d]\n",
			i, led_data->rgbc.color_names[i],
			led_data->rgbc.color_tables[PWM_RGBC_LED_RED][i].period, led_data->rgbc.color_tables[PWM_RGBC_LED_RED][i].duty,
			led_data->rgbc.color_tables[PWM_RGBC_LED_GREEN][i].period, led_data->rgbc.color_tables[PWM_RGBC_LED_GREEN][i].duty,
			led_data->rgbc.color_tables[PWM_RGBC_LED_BLUE][i].period, led_data->rgbc.color_tables[PWM_RGBC_LED_BLUE][i].duty);
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", led_data->trace_buffer);
}

static ssize_t rgbcombo_led_rgbc_color_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);
	struct pwm_rgbc_led *rgbc = &led_data->rgbc;

	unsigned int color, red_duty, red_period, green_duty, green_period, blue_duty, blue_period;

	if (sscanf(buf, "%d %u %u %u %u %u %u",
		&color, &red_period, &red_duty, &green_period, &green_duty, &blue_period, &blue_duty) != 7) {
		dev_err(led_data->dev, "### Keep this format : [color red_duty red_period green_duty green_period blue_duty blue_period] (Ex: 1 50 100 50 100 50 100 \n");
		goto out;
	}

	if (color >= led_data->rgbc.num_color) {
		dev_err(led_data->dev, "Entered color is not permitted\n");
		goto out;
	}

	cancel_delayed_work_sync(&led_data->sequence_work);

	rgbc->color_tables[PWM_RGBC_LED_RED][color].period = red_period;
	rgbc->color_tables[PWM_RGBC_LED_RED][color].duty = red_duty;
	rgbc->color_tables[PWM_RGBC_LED_GREEN][color].period = green_period;
	rgbc->color_tables[PWM_RGBC_LED_GREEN][color].duty = green_duty;
	rgbc->color_tables[PWM_RGBC_LED_BLUE][color].period = blue_period;
	rgbc->color_tables[PWM_RGBC_LED_BLUE][color].duty = blue_duty;

	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm,
		rgbc->color_tables[PWM_RGBC_LED_RED][color].duty,
		rgbc->color_tables[PWM_RGBC_LED_RED][color].period);
	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm,
		rgbc->color_tables[PWM_RGBC_LED_GREEN][color].duty,
		rgbc->color_tables[PWM_RGBC_LED_GREEN][color].period);
	pwm_config(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm,
		rgbc->color_tables[PWM_RGBC_LED_BLUE][color].duty,
		rgbc->color_tables[PWM_RGBC_LED_BLUE][color].period);

	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_RED].pwm);
	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_GREEN].pwm);
	pwm_enable(rgbc->color_pwms[PWM_RGBC_LED_BLUE].pwm);

out:
	return size;
}
static DEVICE_ATTR(color_data, S_IRUGO | S_IWUSR, rgbcombo_led_rgbc_color_data_show, rgbcombo_led_rgbc_color_data_store);

static ssize_t rgbcombo_led_rgbc_debug_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", led_data->debug_enable);
}

static ssize_t rgbcombo_led_rgbc_debug_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct rgbcombo_data *led_data = dev_get_drvdata(dev);
	unsigned int val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	led_data->debug_enable = val;

	return size;
}
static DEVICE_ATTR(debug_enable, S_IRUGO | S_IWUSR,
	rgbcombo_led_rgbc_debug_enable_show, rgbcombo_led_rgbc_debug_enable_store);

static struct attribute *rgbcombo_led_debug_attributes[] = {
	&dev_attr_color.attr,
	&dev_attr_color_data.attr,
	&dev_attr_debug_enable.attr,
	NULL
};

static const struct attribute_group rgbcombo_led_attribute_group = {
	.attrs = rgbcombo_led_debug_attributes,
};

static int rgbcombo_led_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int rgbcombo_led_release (struct inode *inode, struct file *file)
{
	return 0;
}

static long rgbcombo_led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rgbcombo_data *led_data = container_of(file->private_data,
						   struct rgbcombo_data, miscdev);

	long status = 0;

	/* acquire lock */
	mutex_lock(&led_data->lock);

	/* arg validity check */
	if ((arg == (unsigned long)NULL)) {
		dev_err(led_data->dev, "null argument provided\n");
		status = -EINVAL;
		goto null_arg_fail;
	}

	/* determine the IOCTL sent */
	switch (cmd) {
		case RGBCOMBO_LED_IOCTL_GET_STATUS:
		{
			/* copy the arg from kernel space to  user space. */
			status = copy_to_user((void*)arg, &led_data->current_state, sizeof(struct rgbcombo_led_control));
			if (status) {
				dev_err(led_data->dev, "copy to user failed\n");
				status = -EFAULT;
				goto copy_to_user_fail;
			}
		}
			break;
		case RGBCOMBO_LED_IOCTL_SEQUENCE_CONTROL:
		{
			struct rgbcombo_led_sequence_data sequence_data_local;
			struct rgbcombo_led_sequence_data *sequence_data = &sequence_data_local;
			unsigned char seq_idx = 0;

			/* copy the arg from user space to kernel space */
			status = copy_from_user(sequence_data, (void *)arg,
						sizeof(struct rgbcombo_led_sequence_data));
			if (status != 0) {
				dev_err(led_data->dev, "copy from user failed\n");
				status = -EFAULT;
				goto copy_from_user_fail;
			}

			if (sequence_data->num_sequence >= RGBCOMBO_LED_MAX_SEQUENCE) {
				dev_err(led_data->dev, "Num sequence is over Max value\n");
				status = -EINVAL;
				goto copy_from_user_fail;
			}

			/* stop previous sequence and update sequence information */
			cancel_delayed_work_sync(&led_data->sequence_work);

			led_data->num_sequence = sequence_data->num_sequence;
			led_data->num_loop = sequence_data->num_loop;
			led_data->current_sequence = 0;

			dev_info(led_data->dev, "[IOCTL] SEQNUM: %d, LOOPNUM: %d\n", led_data->num_sequence, led_data->num_loop);

			for (seq_idx = 0; seq_idx < sequence_data->num_sequence; seq_idx++) {
				memcpy(&led_data->sequences[seq_idx], &sequence_data->sequences[seq_idx], sizeof(struct rgbcombo_led_control));

				/* For Sequence debug */
				if (led_data->debug_enable) {
					int len = 0, led_idx = 0;

					if (!led_data->trace_buffer) {
						led_data->trace_buffer = devm_kzalloc(led_data->dev, TRACE_TMP_BUFFER_SIZE, GFP_KERNEL);
						if (!led_data->trace_buffer) {
							SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "Failed to allocate memory trace_tmp_buf.\n");
							break;
						}
					}
					memset(led_data->trace_buffer, 0x00, TRACE_TMP_BUFFER_SIZE);

					len += snprintf(&led_data->trace_buffer[len], TRACE_TMP_BUFFER_SIZE - len,
							"[IOCTL] SEQ [%d], Dealy ms[%d]:\t", seq_idx, led_data->sequences[seq_idx].delay_time);

					for (led_idx = 0; led_idx < led_data->num_leds; led_idx++) {
						if (led_data->led_types[led_idx] == RGBC_LED_TYPE) {
						len += snprintf(&led_data->trace_buffer[len], TRACE_TMP_BUFFER_SIZE - len,
							"[%d][%s,%s] ", led_idx,
							led_data->sequences[seq_idx].leds[led_idx].on == LED_STATE_ON ? "On" : "Off",
							led_data->rgbc.color_names[led_data->sequences[seq_idx].leds[led_idx].color]);
						} else {
							len += snprintf(&led_data->trace_buffer[len], TRACE_TMP_BUFFER_SIZE - len,
							"[%d][%s] ", led_idx,
							led_data->sequences[seq_idx].leds[led_idx].on == LED_STATE_ON ? "On" : "Off");
						}
					}
					SEQUENCE_DBG(led_data->debug_enable, led_data->dev, "%s", led_data->trace_buffer);
				}
			}
#if USE_OWN_WORK_QUEUE
			queue_delayed_work(led_data->work_queue, &led_data->sequence_work, 0);
#else
			schedule_delayed_work(&led_data->sequence_work, 0);
#endif
		}
			break;
		default:
		{
			dev_err(led_data->dev, "Invalid ioctl received 0x%08X\n",
					cmd);
			status = -EINVAL;
		}
			break;
	}

null_arg_fail:
copy_from_user_fail:
copy_to_user_fail:
	/* Release lock*/
	mutex_unlock(&led_data->lock);

	return status;
}

static const struct file_operations rgbcombo_led_fops = {
	.owner = THIS_MODULE,
	.open  = rgbcombo_led_open,
	.release = rgbcombo_led_release,
	.unlocked_ioctl = rgbcombo_led_ioctl,
};

static int rgbcombo_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rgbcombo_data *led_data = NULL;
	int ret = 0;

	led_data = devm_kzalloc(&pdev->dev, sizeof(struct rgbcombo_data), GFP_KERNEL);
	if (!led_data) {
		dev_err(dev, "Failed to allocate memory.\n");
		return -ENOMEM;
	}
	led_data->dev = dev;

	/* Get device tree data */
	ret = rgbcombo_led_parse_dt(led_data);
	if (ret)
		goto err_out;

	/* Set default state */
	rgbcombo_led_set_default_state(led_data);

	/* Regist misc device */
	led_data->miscdev.minor = RGBCOMBO_LED_MINOR,
	led_data->miscdev.name = RGBCOMBO_LED_NAME,
	led_data->miscdev.fops = &rgbcombo_led_fops,
	led_data->miscdev.mode = 0666,
#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	led_data->miscdev.lab_smk64 = "*",
#endif
	ret = misc_register(&led_data->miscdev);
	if (ret) {
		dev_err(dev, "Failed to create misc device.\n");
		goto err_out;
	}
	if (sysfs_create_group(&led_data->dev->kobj, &rgbcombo_led_attribute_group)) {
		dev_err(dev, "Failed to create attribute group\n");
	}
	platform_set_drvdata(pdev, led_data);

	mutex_init(&led_data->lock);
	INIT_DELAYED_WORK(&led_data->sequence_work, rgbcombo_led_set_sequence_work);

#if USE_OWN_WORK_QUEUE
	led_data->work_queue = create_singlethread_workqueue("rgbcombo_led");
	if (!led_data->work_queue) {
		dev_err(dev, "Could not create work_queue\n");
		ret = -EINVAL;
		goto err_out;
	}
#endif

	return ret;

err_out:
	return ret;
}

static int rgbcombo_led_remove(struct platform_device *pdev)
{
	struct rgbcombo_data *led_data = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&led_data->sequence_work);
#if USE_OWN_WORK_QUEUE
	destroy_workqueue(led_data->work_queue);
#endif
	misc_deregister(&led_data->miscdev);

	return 0;
}

static const struct of_device_id of_rgbcombo_leds_match[] = {
	{ .compatible = "rgbcombo-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_rgbcombo_leds_match);

static struct platform_driver rgbcombo_led_driver = {
	.probe		= rgbcombo_led_probe,
	.remove		= rgbcombo_led_remove,
	.driver		= {
		.name	= "leds-rgbcombo",
		.of_match_table = of_rgbcombo_leds_match,
	},
};

module_platform_driver(rgbcombo_led_driver);

MODULE_LICENSE("GPL");
