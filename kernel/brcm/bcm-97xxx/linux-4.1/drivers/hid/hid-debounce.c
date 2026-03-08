/*
 * HID driver for remote debounce
 *
 * Copyright (c) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/* #define DEBUG */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>

#include <linux/hid.h>
#include "hid-ids.h"

/* how long to debounce the keys for */
#define DEBOUNCE_KEY_TIME_MS	200

static const struct hid_device_id hid_table[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_FX, USB_DEVICE_ID_FX_REMOTE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, hid_table);

struct debounce_data {
	struct timespec start_time;
	bool drop_keys;
};

static int debounce_hid_probe(struct hid_device *hdev,
	const struct hid_device_id *id)
{
	int ret;
	struct debounce_data *dbdata;

	hid_dbg(hdev, "%s enter", __func__);

	dbdata = devm_kzalloc(&hdev->dev, sizeof(*dbdata), GFP_KERNEL);
	if (!dbdata) {
		hid_err(hdev, "%s no memory for internal data", __func__);
		return -ENOMEM;
	}

	/* initialize the internal data */
	ktime_get_ts(&dbdata->start_time);
	dbdata->drop_keys = true;

	hid_set_drvdata(hdev, dbdata);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "%s failed to parse HID", __func__);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "%s failed to start HW", __func__);
		return ret;
	}

	return 0;
}

static void debounce_hid_remove(struct hid_device *hdev)
{
	struct debounce_data *dbdata;

	hid_dbg(hdev, "%s enter", __func__);

	dbdata = hid_get_drvdata(hdev);

	hid_hw_stop(hdev);
}

static int debounce_hid_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct debounce_data *dbdata;
	struct timespec now;
	struct timespec delta;
	int ms_elapsed;

	hid_dbg(hdev, "%s enter", __func__);

	dbdata = hid_get_drvdata(hdev);
	if (!dbdata) {
		hid_warn(hdev, "%s no internal data found", __func__);
		return -1;
	}

	if (!(hdev->claimed & HID_CLAIMED_INPUT)) {
		/* we are not input device */
		hid_dbg(hdev, "%s not an input device", __func__);
		return 0;
	}

	/* determine if we should continue to drop keys or not */
	if (dbdata->drop_keys) {
		ktime_get_ts(&now);
		delta = timespec_sub(now, dbdata->start_time);
		ms_elapsed = delta.tv_sec * 1000 + delta.tv_nsec / 1000000;

		if (ms_elapsed > DEBOUNCE_KEY_TIME_MS) {
			/* no longer need to drop keys */
			dbdata->drop_keys = false;
		}
	}

	if (dbdata->drop_keys) {
		/* silently drop the key but pretend that we handled it */
		hid_dbg(hdev, "%s dropping keys type %d code %d (ms=%d)",
				__func__,
				usage->type,
				usage->code,
				ms_elapsed);
	} else {
		hid_dbg(hdev, "%s inputting event type %d code %d",
				__func__,
				usage->type,
				usage->code);

		input_event(field->hidinput->input,
					usage->type,
					usage->code,
					value);
	}

	return 1;
}

static struct hid_driver hid_debounce = {
	.name = "hid-debounce",
	.id_table = hid_table,
	.probe = debounce_hid_probe,
	.remove = debounce_hid_remove,
	.event = debounce_hid_event,
};
module_hid_driver(hid_debounce);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("HID debounce driver");
MODULE_LICENSE("GPLv2");
