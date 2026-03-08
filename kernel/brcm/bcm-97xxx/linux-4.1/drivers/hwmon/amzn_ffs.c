/*
 * amzn_ffs.c - Hwmon driver for detecting Free Fall Sensor
 *
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define FFS_METRICS_STR_LEN 256
char *ffs_metrics_prefix = "frankhdd";
extern const char *product_id2;
#endif

#define FFS_GPIO	177
int bma222e_log_fall(void);
struct work_struct ffs_work;

static irqreturn_t ffs_irq_handler(int irq, void *dev_id)
{
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[FFS_METRICS_STR_LEN];

	/* Log in metrics */
	snprintf(buf, FFS_METRICS_STR_LEN,"%s:%s:free_fall_event=1;CT;1:NR", ffs_metrics_prefix, product_id2? product_id2: "def");
	log_to_metrics(ANDROID_LOG_INFO, "HDDEvent", buf);
#endif

	printk("WARNING: Free Fall event detected\n");
	schedule_work(&ffs_work);
	return IRQ_HANDLED;
}

static void ffs_work_handler( struct work_struct *work)
{
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[FFS_METRICS_STR_LEN];
	int max_g;

	max_g = bma222e_log_fall();
	/* Log in metrics */
	snprintf(buf, FFS_METRICS_STR_LEN,"%s:%s:free_fall_mG=%d;CT;1:NR", ffs_metrics_prefix, product_id2? product_id2: "def", max_g);
	log_to_metrics(ANDROID_LOG_INFO, "HDDEvent", buf);
#endif
}

static int ffs_probe(struct platform_device *pdev)
{

	if( request_irq(gpio_to_irq(FFS_GPIO), ffs_irq_handler, IRQF_TRIGGER_FALLING,
		"Free Fall Sensor IRQ handler", NULL) )
	{
		printk(KERN_ERR "%s: unable to register IRQ\n",__FUNCTION__);
		return -EIO;
	}
	INIT_WORK(&ffs_work, ffs_work_handler);

	return 0;
}

static int ffs_remove(struct platform_device *pdev)
{
	free_irq(gpio_to_irq(FFS_GPIO), NULL);
	return 0;
}

static const struct of_device_id of_ffs_match[] = {
	{ .compatible = "ffs", },
	{},
};

static struct platform_driver ffs_driver = {
	.probe		= ffs_probe,
	.remove		= ffs_remove,
	.driver = {
		.name           = "ffs",
		.of_match_table = of_ffs_match,
	},
};

module_platform_driver(ffs_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ffs");
MODULE_DESCRIPTION("Free Fall Sensor driver");
