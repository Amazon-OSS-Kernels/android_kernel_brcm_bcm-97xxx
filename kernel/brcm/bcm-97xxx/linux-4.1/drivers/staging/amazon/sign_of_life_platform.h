/*
 * sign_of_life_platform.h
 *
 * platfrom specific lrc data header file
 *
 * Copyright (C) 2015-2017 Amazon Technologies Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SIGN_OF_LIFE_PLATFORM_H
#define __SIGN_OF_LIFE_PLATFORM_H

/* platform specific lcr_data */
static struct sign_of_life_reason_data lcr_data[] = {
	{LIFE_CYCLE_NOT_AVAILABLE, "Life Cycle Reason Not Available", "LCR_abnormal"},
	{SHUTDOWN_BY_SW, "Software Shutdown", "LCR_normal"},
	{SHUTDOWN_BY_LONG_PWR_KEY_PRESS, "Long Pressed Power Key Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_PMIC, "PMIC Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_SOC, "SOC Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_PCB, "PCB Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_WIFI, "WIFI Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_HDD, "HDD Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_OTHER1, "F-Connector Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_OTHER2, "Enclosure Overheated Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_FAN, "Fan Failure caused Thermal Shutdown", "LCR_abnormal"},
	{THERMAL_SHUTDOWN_REASON_SENSOR, "Sensor Failure Triggered Shutdown", "LCR_abnormal"},
	{SHUTDOWN_BY_SUDDEN_POWER_LOSS, "Sudden Power Loss Shutdown", "LCR_abnormal"},
	{SHUTDOWN_BY_UNKNOWN_REASONS, "Unknown Shutdown", "LCR_abnormal"},
	{COLDBOOT_BY_POWER_KEY, "Cold Boot By Power Key", "LCR_normal"},
	{COLDBOOT_BY_POWER_SUPPLY, "Cold Boot By USB Charger", "LCR_normal"},
	{WARMBOOT_BY_SW, "Warm Boot By Software", "LCR_normal"},
	{WARMBOOT_BY_KERNEL_PANIC, "Warm Boot By Kernel Panic", "LCR_abnormal"},
	{WARMBOOT_BY_KERNEL_WATCHDOG, "Warm Boot By Kernel Watchdog", "LCR_abnormal"},
	{WARMBOOT_BY_HW_WATCHDOG, "Warm Boot By HW Watchdog", "LCR_abnormal"},
	{LIFE_CYCLE_SMODE_WARM_BOOT_USB_CONNECTED, "Power Off Charging Mode", "LCR_normal"},
	{LIFE_CYCLE_SMODE_FACTORY_RESET, "Factory Reset Reboot", "LCR_normal"},
	{LIFE_CYCLE_SMODE_OTA, "OTA Reboot", "LCR_normal"},
	{LIFE_CYCLE_SMODE_HDD, "HDD Failure Reboot", "LCR_abnormal"},
	{LIFE_CYCLE_SMODE_TUNER, "TUNER Failure Reboot", "LCR_abnormal"},
};
#endif
