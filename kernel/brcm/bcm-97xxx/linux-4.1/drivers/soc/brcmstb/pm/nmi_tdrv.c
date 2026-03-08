/*
 * linux-4.1/drivers/soc/brcmstb/pm/nmi_tdrv.c
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/fiq.h>
#include <asm/irqflags.h>
#include <asm/cacheflush.h>

#include "nmi_tdrv.h"

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#include <linux/gpio.h>

/* Redefintion of commands and WDOG registers */
/* Keep in sync with bcm7038_wdt.c */
#define WDT_START_1		0xff00
#define WDT_START_2		0x00ff
#define WDT_STOP_1		0xee00
#define WDT_STOP_2		0x00ee
#define WDT_EVENT_NMI		0x1
#define WDT_EVENT_RESET		0x0

#define WDT_TIMEOUT_REG		0x0
#define WDT_CMD_REG		0x4
#define WDT_RST_COUNT_REG	0x8
#define WDT_TIMER_INT_REG	0x10
#define WDT_CTRL_REG		0x14

#define WDT_MIN_TIMEOUT		1 /* seconds */
#define WDT_DEFAULT_TIMEOUT	30 /* seconds */
#define WDT_DEFAULT_RATE	27000000

#define WDT_TIMEOUT_NMI         WDT_MIN_TIMEOUT * WDT_DEFAULT_RATE
#define WDT_BASE_REG_ADDR	0xF040A7A8
#define WDT_BASE_REG_SIZE	0x20

#define NUM_CPU_CORES		2

/*
 * Function Declarations
 */
static int __init nmi_tdrv_init(void);
static void __exit nmi_tdrv_exit(void);
static int nmi_tdrv_deinit(void);

/*
 * Variable Declarations
 */
static void *base_addr;
static void *wdt_addr;
struct	fiq_handler	fiq_handler;
extern	unsigned long	nmi_fiq_start;
extern	unsigned long	nmi_fiq_end;
int	in_wdog_nmi = 0;
__u32	nmi_lock = 0;

struct nmi_coredump_save {
	__u32  spsr;
	struct cpu_context_save	regs; /* cpu context */
	__u32  dumped;
};

static struct nmi_coredump_save nmi_coredump[NUM_CPU_CORES];

extern void nmi_reg_save(struct nmi_coredump_save *dump_ptr);
extern void nmi_lock_acquire(__u32 *nmi_lock);
extern void nmi_lock_release(__u32 *nmi_lock);

/*
 * NMI Test Driver Functions
 */

static int __init nmi_tdrv_init(void)
{
	int ret;
	//volatile uint32_t reg_val;

	/* Map the physcial register base to a kernel virtual io address */
	base_addr = ioremap_nocache(AON_CTRL_REG_BASE_ADDR, AON_CTRL_REG_SIZE);
	wdt_addr  = ioremap_nocache(WDT_BASE_REG_ADDR, WDT_BASE_REG_SIZE);

	/* Claim the FIQ */
	ret = claim_fiq(&fiq_handler);
	if (ret)
	{
		pr_err("nmi_tdrv_init(): couldn't claim FIQ, ret=%d\n", ret);
		return -1;
	}

	/* Assign the handler */
	set_fiq_handler(&nmi_fiq_start,
		(unsigned int)&nmi_fiq_end-(unsigned int)&nmi_fiq_start);

	printk(KERN_INFO "Enabling NMI via ANON_CTRL_NMI_CTRL register\n");
	writel(0, (void*) ((unsigned char *)base_addr+AON_CTRL_NMI_CTRL));

#if 0
	printk(KERN_INFO "ANON_CTRL_NMI_CTRL register=%8.8x\n",
	readl((void*) ((unsigned char *)base_addr+AON_CTRL_NMI_CTRL)));
#endif
	return 0;
}

static void __exit nmi_tdrv_exit(void)
{
	nmi_tdrv_deinit();
}

static int nmi_tdrv_deinit(void)
{
	release_fiq(&fiq_handler);

	if(base_addr) iounmap(base_addr);

	return 0;
}

asmlinkage __visible __naked int nmi_tdrv_log(void)
{
	__u32		num_core;

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
//	life_cycle_set_shutdown_reason(SHUTDOWN_BY_SUDDEN_POWER_LOSS);
#endif
	//memcpy((void*)&nmi_coredump, (void*)cpu_ctxt, sizeof(struct cpu_context_save));

//TODO: Need to enable current cpu context save code below once we fix the underlying issue
#if 0
	num_core = smp_processor_id();

	nmi_lock_acquire(&nmi_lock);
	if (!nmi_coredump[num_core].dumped)
		nmi_reg_save(&(nmi_coredump[num_core]));
	nmi_coredump[num_core].dumped = 1;
	nmi_lock_release(&nmi_lock);
#endif
	in_wdog_nmi = 1;

	/* Flush all caches */
	flush_cache_all();

	writel(WDT_STOP_1, (void*) ((unsigned char *)wdt_addr + WDT_CMD_REG));
	writel(WDT_STOP_2, (void*) ((unsigned char *)wdt_addr + WDT_CMD_REG));

	writel(WDT_EVENT_RESET, (void*) ((unsigned char *)wdt_addr + WDT_CTRL_REG));

	writel(WDT_TIMEOUT_NMI, (void*) ((unsigned char *)wdt_addr + WDT_TIMEOUT_REG));

	writel(WDT_START_1, (void*) ((unsigned char *)wdt_addr + WDT_CMD_REG));
	writel(WDT_START_2, (void*) ((unsigned char *)wdt_addr + WDT_CMD_REG));

	while(1);
}
EXPORT_SYMBOL(nmi_tdrv_log);

module_init(nmi_tdrv_init);
module_exit(nmi_tdrv_exit);
MODULE_LICENSE("GPL");

