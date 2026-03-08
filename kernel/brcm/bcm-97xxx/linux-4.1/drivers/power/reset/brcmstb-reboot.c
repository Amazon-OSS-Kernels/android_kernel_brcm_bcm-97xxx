/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/smp.h>
#include <linux/mfd/syscon.h>
#include <linux/gpio.h>
#include <asm/cacheflush.h>
#include <linux/sign_of_life.h>

#define RESET_SOURCE_ENABLE_REG 1
#define SW_MASTER_RESET_REG 2

/* Other AON Register Definitions can be found in 'mach-bcm/aon_defs.h'*/
#define AON_REG_DRAM_SCRAMBLE_FLAGS	0x20
#define AON_REG_ANDROID_RESTART_CAUSE	0x24
#define AON_REG_ANDROID_RESTART_TIME	0x28
#define AON_REG_ANDROID_RESTART_TIME_N	0x2C
#define AON_REG_ANDROID_RESTART_FAN_PWM 0x3C

#define AON_REG_RAMDUMP_FLAGS_0  0x34
#define AON_REG_RAMDUMP_FLAGS_1  0x38
#define NUM_GPIOS 224

static struct regmap *regmap;
static u32 rst_src_en;
static u32 sw_mstr_rst;
static void __iomem *aon_sram_base;

struct reset_reg_mask {
	u32 rst_src_en_mask;
	u32 sw_mstr_rst_mask;
};

static const struct reset_reg_mask *reset_masks;
static u32 reboot_reason = LIFE_CYCLE_NOT_AVAILABLE;

extern u32 get_pwm(void);

#ifdef CONFIG_BRCMSTB_WKTMR_SYSTIME_SYNC
static inline void brcmstb_save_reboot_time(void)
{
	/* Sync the wall clock into AON SRAM since wktmr gets reset */
	struct timespec now;
	u32 sec;

	getnstimeofday(&now);

	sec = now.tv_sec + (now.tv_nsec + 500000000) / 1000000000;
	writel(sec, aon_sram_base + AON_REG_ANDROID_RESTART_TIME);
	writel(~sec, aon_sram_base + AON_REG_ANDROID_RESTART_TIME_N);
}
#endif

const void set_reboot_reason(u32 reason)
{
	reboot_reason = reason;
}
EXPORT_SYMBOL(set_reboot_reason);

u32 get_reboot_reason(void)
{
	return reboot_reason;
}
EXPORT_SYMBOL(get_reboot_reason);

static int brcmstb_reboot_handler(struct notifier_block *this,
				  unsigned long mode, void *cmd)
{
	u32 val;
	char *command = cmd;

	if (mode != SYS_RESTART)
		return NOTIFY_DONE;

	if (aon_sram_base != NULL) {
		/* Save the reboot reason to AON SRAM for bootloader to use.
		* Do this before writing to the sw master reset reg.
		* If there is an error in writing to sw master reset reg,
		* then we will likely need power-on-reset and the AON SRAM
		* will be cleared as part of the power-on-reset.*/
		if (command != NULL && command[0] != 0) {
			/* Save first letter of the reboot command argument to
			 * distinguish different reboot reason. Expected 'cmd':
			 *   - 'bootloader': Boot into bootloader
			 *   - 'recovery': Boot into recovery mode
			 *   - 'hdd-failure': Boot into noraml Android mode with life cycle reason and create pstore
			 *   - 'tuner-failure': Boot into noraml Android mode with life cycle reason and create pstore
			 *   - 'system' or '': Boot into normal Android mode*/
			val = command[0];
			pr_info("brcmstb_reboot: cmd='%s', val=%u\n", command, val);
		} else {
			val = reboot_reason;
			pr_info("brcmstb_reboot: empty cmd string\n");
		}
		writel(val, aon_sram_base + AON_REG_ANDROID_RESTART_CAUSE);
		u32 fan_pwm = get_pwm();
		writel(fan_pwm, aon_sram_base + AON_REG_ANDROID_RESTART_FAN_PWM);

		if((val < THERMAL_SHUTDOWN_REASON_BATTERY || val > THERMAL_SHUTDOWN_REASON_SENSOR )
                    && val != 'h' && val != 't') {
			/* Clear the ramdump cookies */
			writel(0x0, aon_sram_base + AON_REG_RAMDUMP_FLAGS_0);
			writel(0x0, aon_sram_base + AON_REG_RAMDUMP_FLAGS_1);
			/* Clear the PANIC magic value */
			__raw_writel(0x0, aon_sram_base + AON_REG_DRAM_SCRAMBLE_FLAGS);
		}

#ifdef CONFIG_BRCMSTB_WKTMR_SYSTIME_SYNC
		brcmstb_save_reboot_time();
#endif
	} else {
		pr_info("brcmstb_reboot: reboot reason cannot be saved.\n");
	}

	return NOTIFY_DONE;
}

static int brcmstb_restart_handler(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	int rc, i, val;
	u32 tmp;
	char *command = cmd;

	if (aon_sram_base != NULL) {
		/* Save the reboot reason to AON SRAM for bootloader to use.
		* Do this before writing to the sw master reset reg.
		* If there is an error in writing to sw master reset reg,
		* then we will likely need power-on-reset and the AON SRAM
		* will be cleared as part of the power-on-reset.*/
		if (command != NULL && command[0] != 0) {
			/* Save first letter of the reboot command argument to
			 * distinguish different reboot reason. Expected 'cmd':
			 *   - 'bootloader': Boot into bootloader
			 *   - 'recovery': Boot into recovery mode
			 *   - 'system' or '': Boot into normal Android mode*/
			val = command[0];
			pr_info("brcmstb_reboot: cmd='%s', val=%u\n", command, val);
		} else {
			val = reboot_reason;
			pr_info("brcmstb_reboot: empty cmd string\n");
		}
		writel(val, aon_sram_base + AON_REG_ANDROID_RESTART_CAUSE);
		u32 fan_pwm = get_pwm();
		writel(fan_pwm, aon_sram_base + AON_REG_ANDROID_RESTART_FAN_PWM);
	} else {
		pr_info("brcmstb_reboot: reboot reason cannot be saved.\n");
	}

	pr_info("RESET all GPIOs \n");
	for (i=0; i < NUM_GPIOS; i++) {
		gpio_set_value(i, 0x0);
		gpio_direction_input(i);
	}
	rc = regmap_write(regmap, rst_src_en, reset_masks->rst_src_en_mask);
	if (rc) {
		pr_err("failed to write rst_src_en (%d)\n", rc);
		return NOTIFY_DONE;
	}

	rc = regmap_read(regmap, rst_src_en, &tmp);
	if (rc) {
		pr_err("failed to read rst_src_en (%d)\n", rc);
		return NOTIFY_DONE;
	}

	/* Flush all caches */
	flush_cache_all();

	rc = regmap_write(regmap, sw_mstr_rst, reset_masks->sw_mstr_rst_mask);
	if (rc) {
		pr_err("failed to write sw_mstr_rst (%d)\n", rc);
		return NOTIFY_DONE;
	}

	rc = regmap_read(regmap, sw_mstr_rst, &tmp);
	if (rc) {
		pr_err("failed to read sw_mstr_rst (%d)\n", rc);
		return NOTIFY_DONE;
	}

	while (1)
		;

	return NOTIFY_DONE;
}

static struct notifier_block brcmstb_reboot_nb = {
	.notifier_call = brcmstb_reboot_handler,
	.priority = 128,
};

static struct notifier_block brcmstb_restart_nb = {
	.notifier_call = brcmstb_restart_handler,
	.priority = 128,
};

static const struct reset_reg_mask reset_bits_40nm = {
	.rst_src_en_mask = BIT(0),
	.sw_mstr_rst_mask = BIT(0),
};

static const struct reset_reg_mask reset_bits_65nm = {
	.rst_src_en_mask = BIT(3),
	.sw_mstr_rst_mask = BIT(31),
};

static const struct of_device_id of_match[] = {
	{ .compatible = "brcm,brcmstb-reboot", .data = &reset_bits_40nm },
	{ .compatible = "brcm,bcm7038-reboot", .data = &reset_bits_65nm },
	{},
};

static int brcmstb_reboot_probe(struct platform_device *pdev)
{
	int rc;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct device_node *np_aon_ctrl;

	of_id = of_match_node(of_match, np);
	if (!of_id) {
		pr_err("failed to look up compatible string\n");
		return -EINVAL;
	}
	reset_masks = of_id->data;

	/* The System Data RAM in the AON_CTRL block is used by different
	 * drivers for preserving state across software reset. For backward
	 * compatiblity, we only issue a warning if brcm,brcmstb-aon-ctrl
	 * device tree node cannot be found.*/
	np_aon_ctrl =
		of_find_compatible_node(NULL, NULL, "brcm,brcmstb-aon-ctrl");
	if (!np_aon_ctrl) {
		WARN(1, "brcm,brcmstb-aon-ctrl not found in DT, won't save reboot reason");
		aon_sram_base = NULL;
	} else {
		aon_sram_base = of_iomap(np_aon_ctrl, 1);
		WARN(!aon_sram_base, "failed to map aon-sram base");
		of_node_put(np_aon_ctrl);
	}

	regmap = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(regmap)) {
		pr_err("failed to get syscon phandle\n");
		return -EINVAL;
	}

	rc = of_property_read_u32_index(np, "syscon", RESET_SOURCE_ENABLE_REG,
					&rst_src_en);
	if (rc) {
		pr_err("can't get rst_src_en offset (%d)\n", rc);
		return -EINVAL;
	}

	rc = of_property_read_u32_index(np, "syscon", SW_MASTER_RESET_REG,
					&sw_mstr_rst);
	if (rc) {
		pr_err("can't get sw_mstr_rst offset (%d)\n", rc);
		return -EINVAL;
	}

	rc = register_reboot_notifier(&brcmstb_reboot_nb);
	if (rc)
		dev_err(&pdev->dev,
			"cannot register reboot notifier (err=%d)\n", rc);

	rc = register_restart_handler(&brcmstb_restart_nb);
	if (rc)
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", rc);

	return rc;
}

static struct platform_driver brcmstb_reboot_driver = {
	.probe = brcmstb_reboot_probe,
	.driver = {
		.name = "brcmstb-reboot",
		.of_match_table = of_match,
	},
};

static int __init brcmstb_reboot_init(void)
{
	return platform_driver_probe(&brcmstb_reboot_driver,
					brcmstb_reboot_probe);
}
subsys_initcall(brcmstb_reboot_init);
