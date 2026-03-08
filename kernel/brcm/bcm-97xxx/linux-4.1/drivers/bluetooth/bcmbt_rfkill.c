/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

/*
 * A platform dependent Broadcom Bluetooth rfkill driver which is
 * GPIO based.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

/* Constants and Types */
#define BCMBT_RFKILL_VREG_ON           1
#define BCMBT_RFKILL_VREG_OFF          0
#define BCMBT_RFKILL_N_RESET_ON        0
#define BCMBT_RFKILL_N_RESET_OFF       1
#define BCMBT_RFKILL_UNUSED_GPIO       (-1)

struct bcmbt_rfkill_data {
	int vreg_gpio;
	int n_reset_gpio;
	struct rfkill *rfkill;
	struct device *dev;
};

static int bcmbt_rfkill_set_power(void *pdata, bool blocked)
{
	struct bcmbt_rfkill_data *data = pdata;
	int vreg_gpio = data->vreg_gpio;
	int n_reset_gpio = data->n_reset_gpio;
	bool vreg_valid = (vreg_gpio != BCMBT_RFKILL_UNUSED_GPIO)
			? true : false;
	bool n_reset_valid = (n_reset_gpio != BCMBT_RFKILL_UNUSED_GPIO)
			? true : false;

	if (blocked) { /* Transmitter OFF */
		if (vreg_valid)
			gpio_set_value(vreg_gpio,
				BCMBT_RFKILL_VREG_OFF);
		if (n_reset_valid)
			gpio_set_value(n_reset_gpio,
				BCMBT_RFKILL_N_RESET_ON);
	} else { /* Transmitter ON */
		if (vreg_valid)
			gpio_set_value(vreg_gpio,
				BCMBT_RFKILL_VREG_ON);
		if (n_reset_valid)
			gpio_set_value(n_reset_gpio,
				BCMBT_RFKILL_N_RESET_OFF);
	};

	dev_dbg(data->dev, "blocked:%d vreg:%d n-reset:%d\n",
		blocked,
		(vreg_valid) ? gpio_get_value(vreg_gpio) : -1,
		(n_reset_valid) ? gpio_get_value(n_reset_gpio) : -1);

	return 0;
}

static const struct rfkill_ops bcmbt_rfkill_ops = {
	.set_block = bcmbt_rfkill_set_power,
};

static int bcmbt_rfkill_probe(struct platform_device *pdev)
{
	int rc;
	struct bcmbt_rfkill_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->vreg_gpio = of_get_named_gpio(pdev->dev.of_node,
			"brcm,vreg-gpio", 0);
	if (!gpio_is_valid(data->vreg_gpio)) {
		dev_warn(&pdev->dev, "Invalid vreg-gpio\n");
		data->vreg_gpio = BCMBT_RFKILL_UNUSED_GPIO;
	}

	data->n_reset_gpio = of_get_named_gpio(pdev->dev.of_node,
			"brcm,n-reset-gpio", 0);
	if (!gpio_is_valid(data->n_reset_gpio)) {
		dev_warn(&pdev->dev, "Invalid n-reset-gpio\n");
		data->n_reset_gpio = BCMBT_RFKILL_UNUSED_GPIO;
	}

	if (data->vreg_gpio == BCMBT_RFKILL_UNUSED_GPIO &&
	    data->n_reset_gpio == BCMBT_RFKILL_UNUSED_GPIO) {
		dev_err(&pdev->dev,
		   "vreg_gpio and n_reset_gpio both invalid\n");
		return -EINVAL;
	}

	if (data->vreg_gpio != BCMBT_RFKILL_UNUSED_GPIO) {
		rc = devm_gpio_request_one(&pdev->dev, data->vreg_gpio,
			GPIOF_OUT_INIT_LOW, "bt_vreg_gpio");
		if (rc) {
			dev_err(&pdev->dev, "vreg-gpio request failed\n");
			return rc;
		}

		gpio_direction_output(data->vreg_gpio,
				      BCMBT_RFKILL_VREG_OFF);
	}

	if (data->n_reset_gpio != BCMBT_RFKILL_UNUSED_GPIO) {
		rc = devm_gpio_request_one(&pdev->dev, data->n_reset_gpio,
			GPIOF_OUT_INIT_LOW, "bt-n-reset-gpio");
		if (rc) {
			dev_err(&pdev->dev, "n-reset-gpio request failed\n");
			return rc;
		}
		gpio_direction_output(data->n_reset_gpio,
				      BCMBT_RFKILL_N_RESET_ON);
	}

	data->rfkill = rfkill_alloc("bcmbt-rfkill", &pdev->dev,
			RFKILL_TYPE_BLUETOOTH, &bcmbt_rfkill_ops, data);
	if (!data->rfkill) {
		dev_err(&pdev->dev, "rfkill_alloc failed\n");
		return -ENOMEM;
	}

	/* Keep BT Blocked by default as per above init */
	rfkill_init_sw_state(data->rfkill, false);

	rc = rfkill_register(data->rfkill);
	if (rc) {
		dev_err(&pdev->dev, "rfkill_register failed\n");
		return rc;
	};

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);
	dev_info(&pdev->dev, "Bluetooth rfkill driver registered\n");

	return 0;
}

static int bcmbt_rfkill_remove(struct platform_device *pdev)
{
	struct bcmbt_rfkill_data *data = platform_get_drvdata(pdev);

	rfkill_unregister(data->rfkill);
	rfkill_destroy(data->rfkill);
	return 0;
}

static const struct of_device_id bcmbt_rfkill_of_match[] = {
	{ .compatible = "brcm,bcmbt-rfkill", },
	{},
}
MODULE_DEVICE_TABLE(of, bcmbt_rfkill_of_match);

static struct platform_driver bcmbt_rfkill_driver = {
	.probe = bcmbt_rfkill_probe,
	.remove = bcmbt_rfkill_remove,
	.driver = {
		.name = "bcmbt-rfkill",
		.owner = THIS_MODULE,
		.of_match_table = bcmbt_rfkill_of_match,
	},
};
module_platform_driver(bcmbt_rfkill_driver);

MODULE_DESCRIPTION("Broadcom Bluetooth Rfkill Driver");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPLv2");
