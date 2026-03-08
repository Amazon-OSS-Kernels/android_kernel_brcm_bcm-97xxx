/*
 * Copyright (C) 2018 Broadcom
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, you may obtain a copy at
 * http://www.broadcom.com/licenses/LGPLv2.1.php or by writing to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include "wakeup_driver.h"

#define DRV_NAME	"wakeup_drv"
#define MAJOR_DEVICE 34 /* Major device number */
static char b_devname[16]="wakeup";
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param_string(devname, b_devname, sizeof(b_devname), 0);
#endif


#define MAX_WAKEUP_IRQS		32

struct wakeup_device_info {
	spinlock_t lock;
	struct device_node *dn;
	unsigned int irqs[MAX_WAKEUP_IRQS];
	uint32_t irq_masks[MAX_WAKEUP_IRQS];
	int nr_irqs;

	/* The wakeups that are owned by this module */
	uint32_t wakeups_present;

	/* The wakeups that have been enabled */
	uint32_t wakeups_enabled;

	/*
	 * The wakeups that have been triggered since the last call to
	 * wakeup_ack_status()
	 */
	uint32_t wakeups_status;
};

/* Only need a single instance */
static struct wakeup_device_info wakeup_info;

/*
 * Define a maping between a string representation and a bitmask for use by
 * this module API
 *
 * @wakeup_bit: the WAKEUP_* bitmask assigned to this wakeup source type
 * @name: the string representation of this source (i.e., from device tree)
 */
struct wakeup_source {
	uint32_t wakeup_bit;
	const char *name;
};

/*
 * Map wakeup sources between name (from device tree) and a bitmask
 * NOTE: New WAKEUP_* types should be defined in wakeup_driver.h and mapped to a
 * string here
 */
static struct wakeup_source sources[] = {
	{ WAKEUP_CEC,		"CEC" },
	{ WAKEUP_IRR,		"IRR" },
	{ WAKEUP_KPD,		"KPD" },
	{ WAKEUP_GPIO,      "GPIO" },
	{ WAKEUP_UHFR,		"UHFR" },
	{ WAKEUP_XPT_PMU,	"XPT_PMU" },
};

/*
 * Check which wakeups are present and assigned to this module.
 * Returns result as a bitmask.
 */
uint32_t wakeup_check_present(void)
{
	return wakeup_info.wakeups_present;
}

/* Enable/disable a single wakeup IRQ */
static void wakeup_set_enabled(struct wakeup_device_info *info,
				     uint32_t bitmask, unsigned int enabled)
{
	int i;

	/* Don't mess with interrupts we don't own */
    if(!(bitmask && info->wakeups_present))
            return;

	/* Wakeup is already set? */
    if((enabled  && (info->wakeups_enabled & bitmask)) ||
       (!enabled && !(info->wakeups_enabled & bitmask))) {
            return;
    }

	for (i = 0; i < info->nr_irqs; i++) {
		if (info->irq_masks[i] == bitmask) {
			irq_set_irq_wake(info->irqs[i], enabled);
			if (enabled)
				info->wakeups_enabled |= bitmask;
			else
				info->wakeups_enabled &= ~bitmask;
		}
	}
}

static int wakeups_set_enabled(struct wakeup_device_info *info, uint32_t mask,
			      unsigned int enabled)
{
	spin_lock(&info->lock);
	do {
		int bit = ffs(mask) - 1;
		wakeup_set_enabled(info, 1 << bit, enabled);
		mask &= ~(1 << bit);
	} while (mask);
	spin_unlock(&info->lock);

	return 0;
}

/*
 * Program the wakeup mask, enabling/disabling sources according to the
 * value of each field in @enable
 */
int wakeup_enable(uint32_t mask)
{
	return wakeups_set_enabled(&wakeup_info, mask, 1);
}

int wakeup_disable(uint32_t mask)
{
	return wakeups_set_enabled(&wakeup_info, mask, 0);
}

/*
 * Check and clear wakeup statuses, to see which (if any) sources awoke
 * the system. Returns bitmask representing which device(s) woke the system.
 */
uint32_t wakeup_ack_status(void)
{
	struct wakeup_device_info *info = &wakeup_info;
	uint32_t ret;

	spin_lock(&info->lock);
	ret = info->wakeups_status;
	info->wakeups_status = 0;
	spin_unlock(&info->lock);

	return ret;
}

static irqreturn_t wakeup_irq(int irq, void *data)
{
	struct wakeup_device_info *info = data;
	int i;

	for (i = 0; i < info->nr_irqs; i++) {
		if (info->irqs[i] == irq) {
			spin_lock(&info->lock);
			info->wakeups_status |= info->irq_masks[i];
			spin_unlock(&info->lock);
			return IRQ_HANDLED;
		}
	}
	return IRQ_NONE;
}

static uint32_t struct_to_mask(wakeup_devices *wakeups)
{
    uint32_t mask=0;

    if(wakeups->ir) mask |= WAKEUP_IRR;
    if(wakeups->uhf) mask |= WAKEUP_UHFR;
    if(wakeups->keypad) mask |= WAKEUP_KPD;
    if(wakeups->gpio) mask |= WAKEUP_GPIO;
    if(wakeups->cec) mask |= WAKEUP_CEC;
    if(wakeups->transport) mask |= WAKEUP_XPT_PMU;

    return mask;
}

static int mask_to_struct(uint32_t mask, wakeup_devices *wakeups)
{
    if(mask & WAKEUP_IRR) wakeups->ir = 1;
    if(mask & WAKEUP_UHFR) wakeups->uhf = 1;
    if(mask & WAKEUP_KPD) wakeups->keypad = 1;
    if(mask & WAKEUP_GPIO) wakeups->gpio = 1;
    if(mask & WAKEUP_CEC) wakeups->cec = 1;
    if(mask & WAKEUP_XPT_PMU) wakeups->transport = 1;

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static long wakeup_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
#else
static int wakeup_ioctl(struct inode *inode, struct file * file, unsigned int cmd, unsigned long arg)
#endif
{
    int result = 0;

    switch(cmd) {
        case BRCM_IOCTL_WAKEUP_ENABLE:
            {
                wakeup_devices wakeups;
                uint32_t mask;
                if(copy_from_user(&wakeups, (void*)arg, sizeof(wakeups))) {
                    result = -EFAULT;
                    pr_err("copy_from_user failed!\n");
                    break;
                }
                mask = struct_to_mask(&wakeups);
                result = wakeup_enable(mask);
            }
            break;

        case BRCM_IOCTL_WAKEUP_DISABLE:
            {
                wakeup_devices wakeups;
                uint32_t mask;
                if(copy_from_user(&wakeups, (void*)arg, sizeof(wakeups))) {
                    result = -EFAULT;
                    pr_err("copy_from_user failed!\n");
                    break;
                }
                mask = struct_to_mask(&wakeups);
                result = wakeup_disable(mask);
            }
            break;

        case BRCM_IOCTL_WAKEUP_ACK_STATUS:
            {
                wakeup_devices wakeups;
                uint32_t mask = 0;

                mask = wakeup_ack_status();
                memset(&wakeups, 0, sizeof(wakeups));
                mask_to_struct(mask, &wakeups);

                if(copy_to_user((void*)arg, &wakeups, sizeof(wakeups))) {
                    result = -EFAULT;
                    pr_err("copy_to_user failed!\n");
                    break;
                }
            }
            break;

        case BRCM_IOCTL_WAKEUP_CHECK_PRESENT:
            {
                wakeup_devices wakeups;
                uint32_t mask = 0;

                mask = wakeup_check_present();
                memset(&wakeups, 0, sizeof(wakeups));
                mask_to_struct(mask, &wakeups);

                if(copy_to_user((void*)arg, &wakeups, sizeof(wakeups))) {
                    result = -EFAULT;
                    pr_err("copy_to_user failed!\n");
                    break;
                }
            }
            break;

        default:
            result = -ENOSYS;
            break;
    }
    return result;
}

static int wakeup_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int wakeup_close(struct inode *inode, struct file *file)
{
    return 0;
}

static struct file_operations wakeup_fops = {
    owner:      THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
    unlocked_ioctl:      wakeup_ioctl,
#else
    ioctl:      wakeup_ioctl,
#endif
    open:       wakeup_open,
    release:    wakeup_close,
};

#ifdef B_REFSW_ANDROID
#include <linux/miscdevice.h>
static struct miscdevice wake_miscdevice = {
   .minor = MISC_DYNAMIC_MINOR,
   .name = b_devname,
   .fops = &wakeup_fops,
};
#endif


static int __init wakeup_init(void)
{
	struct device_node *dn;
	struct resource *resources;
	struct wakeup_device_info *info = &wakeup_info;
	int ret, i;

    pr_info("Initializing wakeup driver\n");

	spin_lock_init(&info->lock);

	dn = of_find_node_by_name(NULL, "nexus-wakeups");
	if (!dn) {
		pr_err("could not find Nexus wakeup node\n");
		return -ENXIO;
	}
	info->dn = dn;

	resources = kmalloc(sizeof(*resources) * MAX_WAKEUP_IRQS,
			    GFP_KERNEL);
	if (!resources) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = of_irq_to_resource_table(dn, resources, MAX_WAKEUP_IRQS);
	if (ret < 0)
		goto out_free;

	if (ret == 0) {
		pr_err("no IRQs found\n");
		ret = -ENXIO;
		goto out_free;
	}

	info->nr_irqs = ret;

	for (i = 0; i < info->nr_irqs; i++) {
		int j;
		uint32_t mask = 0;

		/* Match interrupt name with local definitions */
		for (j = 0; j < ARRAY_SIZE(sources); j++) {
			if (!strcasecmp(sources[j].name, resources[i].name)) {
				mask = sources[j].wakeup_bit;
				break;
			}
		}
		if (!mask) {
			pr_err("no match for IRQ '%s'\n", resources[i].name);
			continue;
		}

		info->irqs[i] = resources[i].start;
        /* Save IRQ info . Save this first so we
           can get correct wakeup status in S5.
         */
		info->irq_masks[i] = mask;
		info->wakeups_present |= mask;

		ret = request_irq(info->irqs[i], wakeup_irq, 0, DRV_NAME,
				  info);

		if (ret) {
		    /*
		     * if source is not gpio, print error
		     * otherwise, just means linux has consumed gpio int and is
		     * managing it
		     */
		    if (strcasecmp(resources[i].name, "GPIO"))
		    {
		        pr_err("request_irq failed for '%s'\n", resources[i].name);
		    }
            info->irq_masks[i] = 0;
            info->wakeups_present &= ~mask;
        }
	}

#ifdef B_REFSW_ANDROID
    misc_register(&wake_miscdevice);
#else
    if (register_chrdev(MAJOR_DEVICE, b_devname, &wakeup_fops))
    {
        pr_err("Unable to get major %d\n", MAJOR_DEVICE);
        goto out_free;
    }
#endif

	kfree(resources);

    pr_info("Initialization complete\n");
	return 0;

out_free:
	if (resources) {
		/* Free all IRQs we grabbed */
		for (i = 0; i < info->nr_irqs; i++)
			if (info->irq_masks[i]) {
                if(info->wakeups_enabled & info->irq_masks[i]) {
                    irq_set_irq_wake(info->irqs[i], 0);
                }
				free_irq(resources[i].start, info);
            }
		kfree(resources);
	}

	of_node_put(dn);


	return ret;
}
module_init(wakeup_init);

static void __exit wakeup_exit(void)
{
	struct wakeup_device_info *info = &wakeup_info;
	int i;

    pr_info("Cleanup wakeup driver\n");

#ifdef B_REFSW_ANDROID
    misc_deregister(&wake_miscdevice);
#else
    unregister_chrdev(MAJOR_DEVICE, b_devname);
#endif

	for (i = 0; i < info->nr_irqs; i++)
		if (info->irq_masks[i]) {
            if(info->wakeups_enabled & info->irq_masks[i]) {
                irq_set_irq_wake(info->irqs[i], 0);
            }
			free_irq(info->irqs[i], info);
        }

	of_node_put(info->dn);

    pr_info("Cleanup complete\n");
}
module_exit(wakeup_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wake-up driver shim for Nexus");
