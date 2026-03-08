/*
 * Fan Tachometer driver.
 *
 * This module handles interrupt form the Fan Tachometer driven GPIO and
 * calculated the fan speed in RPM.
 *
 * Method of Computation:
 * The fan rpm calculated by this driver is the average fan speed over the
 * last 7 fan revolutions. That is equivelent to the average rpm over the
 * last 0.84 second when fan is running around 500rpm.
 *
 * FAN_TACH Signal Noise Handling:
 * 1. Quite often signal noise can cause false IRQ trigger on the rising
 *    edge while the irq handler only expect triggering on falling edges.
 *    Workaround has been implemented in the irq handler to detect such
 *    false trigers, and igore them.
 * 2. Sometimes signal noice can also cause a falling edge trigger to be
 *    missed, and very rarely even missing 2 triggers close by. SW mitigates
 *    such condition by exluding the longest 2 "per-revolution time periods",
 *    and only use the remaining 5 per-revolution time periods for rpm
 *    calculation.
 * 3. Other minor signal noise intruduced issues, such as single edge slight
 *    shift, causes smaller measurement errors. These errors would be signi-
 *    ficant if fan rpm is computed over a single fan revolution period. Since
 *    this driver computes the average rpm over a 7 fan revolution period,
 *    these smaller errors are reduced to a negligible level.
 *
 * Copyright (C) 2017 Lab126
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/cputime.h>
#include "fan_tach.h"
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define FAN_TACH_GPIO 	90
#define US_PER_SEC 		1000000
#define US_PER_MIN		60000000
#define NS_PER_US		1000
#define FAN_REV_PER_EVERY_TWO_IRQ		1
#define SPEED_UNAVAILABLE	-1

/*
 * DATA_EXPIRATION_SEC is maximum time span we allow across
 * the 8 time stamps. At lowest speed (300 RPM), the time
 * per revolution is 200ms, or 1.4 seconds across 8 time
 * stamps. Thus 2 seconds should be long enough to determime
 * that fan has completely stopped.
 */
#define DATA_EXPIRATION_SEC     2

#define NUM_OF_TIME_STAMPS	8
#define NOTS			NUM_OF_TIME_STAMPS
#define NUM_OF_TIME_DIFFS	NUM_OF_TIME_STAMPS-1
#define NOTD			NUM_OF_TIME_DIFFS


static DEFINE_SPINLOCK(fan_tach_lock);

/* Holds most recent 8 time stamps taken in IRQ handler*/
static struct timespec ts[NOTS];

/* index of most recent IRQ time stamp */
static unsigned int cur = NOTS-1;

/*
 * We take time stamp in every other IRQ.
 */
static int skip_irq = 1;

/*
 * Indicating if we have taken at least 8 time stamps
 * (time elapsed for 8 fan revs). We need 8 time stamps
 * in order to compute a fairly accurate fan speed.
 */
static bool eight_time_stamps_acquired  = false;
struct timespec ts_c[NOTS];

static int get_delta(unsigned int older, unsigned int newer)
{
	return (((int)(ts_c[newer].tv_sec-ts_c[older].tv_sec)) * US_PER_SEC
		+ ((int)(ts_c[newer].tv_nsec-ts_c[older].tv_nsec)) / NS_PER_US);
}

static int sum_of_selected_five_deltas(unsigned int first, unsigned int last)
{
	int overall_delta = 0;
	int cur_delta = 0;
	int largest_delta = 0;
	int second_largest_delta = 0;
	unsigned int i = first;

	while (((i+1)%NOTS) != first) {
		cur_delta = get_delta(i, ((i+1)%NOTS));
		if (cur_delta >= largest_delta) {
			second_largest_delta = largest_delta;
			largest_delta = cur_delta;
		} else if (cur_delta > second_largest_delta) {
			second_largest_delta = cur_delta;
		}
		i = ((i+1)%NOTS);
	}

	overall_delta = get_delta(first, last);

	return (overall_delta - largest_delta - second_largest_delta);
}

/*
 * API for the rest of the kernel to call to get fan speed
 */
int get_fan_rpm(void)
{
	unsigned long flags;

	/* sum of 5 lowest deltas out of 7 deltas in microseconds */
	int delta_sum_selected_5 = 0;

	/* index of IRQ time stamps */
	unsigned int first, last;

	struct timespec now;
        bool got_eight_time_stamps;

	/* recored current time, when fan speed is requested */
	get_monotonic_boottime(&now);

        /* copy out time stamps and current index */
	spin_lock_irqsave(&fan_tach_lock, flags);
	memcpy(ts_c, ts, NOTS*sizeof(struct timespec));
	last = cur;
	got_eight_time_stamps = eight_time_stamps_acquired;
	spin_unlock_irqrestore(&fan_tach_lock, flags);

	/*
	 *  Before the first 8 time stamps are recorded,
	 *  fan speed connot be computed.
	 */
	if (!got_eight_time_stamps){
		return SPEED_UNAVAILABLE;
	}

	first = (last+1)%NOTS;

	/* haven't received IRQ for too long indicating fan stopped.*/
	if ((now.tv_sec - ts_c[first].tv_sec) > DATA_EXPIRATION_SEC ){
		return 0;
	}

	/*
	 * last time stamp is less than or equals to
	 * first stamp indicating invalid data.
	*/
	if ( (ts_c[last].tv_sec < ts_c[first].tv_sec) ||
		 ((ts_c[last].tv_sec == ts_c[first].tv_sec) &&
		  (ts_c[last].tv_nsec <= ts_c[first].tv_nsec))){
		return SPEED_UNAVAILABLE;
	}

	delta_sum_selected_5 = sum_of_selected_five_deltas(first, last);

	/* remove the risk of "divide by zero" crash*/
	if ( 0 == delta_sum_selected_5){
		return SPEED_UNAVAILABLE;
	}

	return (int)(((FAN_REV_PER_EVERY_TWO_IRQ * 5) * US_PER_MIN) / delta_sum_selected_5);
}

static ssize_t show_rpm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", get_fan_rpm());
}

static SENSOR_DEVICE_ATTR(rpm, S_IRUGO, show_rpm, NULL, 0);

static struct attribute *fan_tach_attrs[] = {
	&sensor_dev_attr_rpm.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(fan_tach);

static irqreturn_t fan_tach_irq_handler(int irq, void *dev_id)
{
	unsigned long flags;

        /*
         * do nothing when irq is mistakenly triggered on
         * rising edge of fan_tach signal.
         */
	if (gpio_get_value(FAN_TACH_GPIO)){
		return IRQ_HANDLED;
	}

	skip_irq = (skip_irq+1)%2;
	if(skip_irq){
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&fan_tach_lock, flags);

	cur = (cur+1)%8;
	get_monotonic_boottime(&ts[cur]);

	if (false == eight_time_stamps_acquired){
		if (cur == 7){
			eight_time_stamps_acquired = true;
		}
	}

   	spin_unlock_irqrestore(&fan_tach_lock, flags);

	return IRQ_HANDLED;
}

static int fan_tach_probe(struct platform_device *pdev)
{
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_groups(&pdev->dev, "fan_tach",
						       NULL, fan_tach_groups);

	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device\n");
		return PTR_ERR(hwmon);
	}

	if (request_irq
	    (gpio_to_irq(FAN_TACH_GPIO), fan_tach_irq_handler, IRQF_TRIGGER_FALLING,
	     "FAN TACH IRQ HANDLER", NULL)) {
		printk(KERN_ERR "fan_tach: Unable to register IRQ\n");
		return -EIO;
	}

	printk(KERN_INFO "fan_tach: registered for IRQ\n");
	return 0;
}

static int fan_tach_remove(struct platform_device *pdev)
{
	printk(KERN_INFO "fan_tach unloaded\n");
	free_irq(gpio_to_irq(FAN_TACH_GPIO), NULL);
	return 0;
}

EXPORT_SYMBOL(get_fan_rpm);

static const struct of_device_id of_fan_tach_match[] = {
	{ .compatible = "fan_tach", },
	{},
};

static struct platform_driver fan_tach_driver = {
	.probe		= fan_tach_probe,
	.remove		= fan_tach_remove,
	.driver	= {
		.name		= "fan_tach",
		.of_match_table	= of_fan_tach_match,
	},
};

module_platform_driver(fan_tach_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:fan-tach");
MODULE_AUTHOR("Qi Zeng <qizeng@amazon.com>");
MODULE_DESCRIPTION("Fan Tachometer driver");
