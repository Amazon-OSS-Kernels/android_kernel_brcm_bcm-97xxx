/*
 * linux-4.1/drivers/soc/brcmstb/pm/nmi_tdrv.h
 *
 * Copyright 2018 Amazon.com, Inc. or its Affiliates. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

#ifndef NMI_TDRV_H
#define NMI_TDRV_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/printk.h>

#define AON_CTRL_REG_BASE_ADDR 0xF0410000
#define AON_CTRL_REG_SIZE      0x600

#define AON_CTRL_NMI_CTRL 0x70

#ifndef UNUSED
#define UNUSED(x) (void)x
#endif

#define LOGD(format, ...) pr_info ("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__)
#define LOGW(format, ...) pr_warn ("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__)
#define LOGE(format, ...) pr_err  ("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__)
#define LOGI(format, ...) pr_info ("%s: " format "\n", __FUNCTION__, ## __VA_ARGS__)

int nmi_tdrv_log(void);

#endif /* NMI_TDRV_H */
