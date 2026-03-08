/*
 * sign_of_life_frank.c
 *
 * frank platform implementation
 *
 * Copyright 2015-2017 Amazon Technologies, Inc. All Rights Reserved.
 * Original code: Yang Liu (yangliu@lab126.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/sign_of_life.h>
#include <linux/of.h>

extern void set_reboot_reason(u32 reason);
extern u32 get_reboot_reason(void);

typedef struct {
  life_cycle_reason_t reason;
  char *reason_description;
} life_cycle_reason_description_t;

/* Need in sync with reason_str() defined in android_boot_common.c */
static life_cycle_reason_description_t lcrd[] = {
	{ WARMBOOT_BY_KERNEL_PANIC, "kernel_panic" },
	{ WARMBOOT_BY_KERNEL_WATCHDOG, "wdt" },
	{ WARMBOOT_BY_HW_WATCHDOG, "watchdog" },
	{ WARMBOOT_BY_SW, "reboot" },
	{ COLDBOOT_BY_USB, "usb" },
	{ COLDBOOT_BY_POWER_KEY, "power_key" },
	{ COLDBOOT_BY_POWER_SUPPLY, "power_supply" },
	{ THERMAL_SHUTDOWN_REASON_PMIC, "thermal_shutdown_pmic" },
	{ THERMAL_SHUTDOWN_REASON_SOC, "thermal_shutdown_soc" },
	{ THERMAL_SHUTDOWN_REASON_PCB, "thermal_shutdown_pcb" },
	{ THERMAL_SHUTDOWN_REASON_WIFI, "thermal_shutdown_wifi" },
	{ THERMAL_SHUTDOWN_REASON_HDD, "thermal_shutdown_hdd" },
	{ THERMAL_SHUTDOWN_REASON_OTHER1, "thermal_shutdown_other1" },
	{ THERMAL_SHUTDOWN_REASON_OTHER2, "thermal_shutdown_other2" },
	{ THERMAL_SHUTDOWN_REASON_FAN, "thermal_shutdown_fan" },
	{ THERMAL_SHUTDOWN_REASON_SENSOR, "thermal_shutdown_sensor" },
	{ SHUTDOWN_BY_SUDDEN_POWER_LOSS, "shutdown_sudden_power_loss" },
	{ SHUTDOWN_BY_UNKNOWN_REASONS, "shutdown_unknown_reason" },
	{ SHUTDOWN_BY_SW, "shutdown_sw" },
	{ SHUTDOWN_BY_LONG_PWR_KEY_PRESS, "shutdown_long_pwr_key_press" },
	{ LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED, "lc_smode_warm_boot_usb_connected" },
	{ LIFE_CYCLE_SMODE_FACTORY_RESET, "lc_smode_factory_reset" },
	{ LIFE_CYCLE_SMODE_OTA, "lc_smode_ota" },
	{ LIFE_CYCLE_SMODE_HDD, "lc_smode_hdd" },
	{ LIFE_CYCLE_SMODE_TUNER, "lc_smode_tuner" },
	{ LIFE_CYCLE_NOT_AVAILABLE, "unknown" }
};

static int frank_read_boot_reason(life_cycle_reason_t *reason)
{
	struct device_node *android;
	const char *bootreason;
	int i = 0;

	*reason = LIFE_CYCLE_NOT_AVAILABLE;

	android = of_find_node_by_path("/firmware/android");
	if (android) {
		bootreason = of_get_property(android, "bootreason", NULL);
		of_node_put(android);
	}

	while(lcrd[i].reason != LIFE_CYCLE_NOT_AVAILABLE) {
		if (!strcmp(lcrd[i].reason_description, bootreason)) {
			*reason = lcrd[i].reason;
			break;
		}
		i++;
	}
	return 0;
}

static int frank_write_boot_reason(life_cycle_reason_t reason)
{
	/*
	 * preserve life cycle reason if it has already been set
	 * e.g. thermal shutdown code sets shutdown reason, then
	 * call orderly reboot
	 */
	if(get_reboot_reason() == LIFE_CYCLE_NOT_AVAILABLE)
		set_reboot_reason(reason);
	return 0;
}

static int frank_write_thermal_boot_reason(life_cycle_reason_t reason)
{
	static int fan_fail = false;

	if( fan_fail == false ) {
		set_reboot_reason(reason);
		if( reason == THERMAL_SHUTDOWN_REASON_FAN )
			fan_fail = true;
	}
	return 0;
}

static int frank_lcr_reset(void)
{
	set_reboot_reason(LIFE_CYCLE_NOT_AVAILABLE);
	return 0;
}

static int frank_read_special_mode(life_cycle_reason_t *mode)
{
	// not supported on Frank
	return 0;
}

static int frank_write_special_mode(life_cycle_reason_t mode)
{
	return 0;
}

int life_cycle_platform_init(struct sign_of_life_ops *sol_ops)
{
	// we're using a common boot_reason on frank, so we don't need different
	// function handlers for thermal boot vs shutdown vs thermal
	sol_ops->read_boot_reason 		= frank_read_boot_reason;
	sol_ops->write_boot_reason 		= frank_write_boot_reason;
	sol_ops->read_shutdown_reason 		= frank_read_boot_reason;
	sol_ops->write_shutdown_reason 		= frank_write_boot_reason;
	sol_ops->read_thermal_shutdown_reason 	= frank_read_boot_reason;
	sol_ops->write_thermal_shutdown_reason 	= frank_write_thermal_boot_reason;
	sol_ops->read_special_mode 		= frank_read_special_mode;
	sol_ops->write_special_mode 		= frank_write_special_mode;
	sol_ops->lcr_reset 			= frank_lcr_reset;
	return 0;
}
EXPORT_SYMBOL(life_cycle_platform_init);
