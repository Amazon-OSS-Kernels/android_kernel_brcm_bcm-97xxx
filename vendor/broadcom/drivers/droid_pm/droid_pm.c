/*
 * Copyright (C) 2015 Broadcom Corporation
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

// Uncomment the line below to enable device debugging
//#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/eventfd.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/types.h>
#include <linux/version.h>

#include "droid_pm.h"


#define mkver(maj,min) #maj "." #min
#define MKVER(maj,min) mkver(maj,min)

#define DRV_MAJOR               6
#define DRV_MINOR               0
#define DRV_VERSION             MKVER(DRV_MAJOR, DRV_MINOR)
#define SUSPEND_TIMEOUT_MS      (10 * 1000)
#define MAX_WAKEUP_IRQS         32

#ifdef CONFIG_BRCMSTB_MAP_MEM_TO_S2
static const bool b_map_mem_to_s2 = true;
#else
static const bool b_map_mem_to_s2 = false;
#endif

static char b_devname[16]     = DROID_PM_DRV_NAME;
static int suspend_timeout_ms = SUSPEND_TIMEOUT_MS;

/* Module parameters that can be specified at insmod time */
module_param_string(devname, b_devname, sizeof(b_devname), 0);
module_param(suspend_timeout_ms, int, 0);

MODULE_PARM_DESC(suspend_timeout_ms,
    "Suspend timeout value in milliseconds. (default="
    __MODULE_STRING(SUSPEND_TIMEOUT_MS) ")");

/*
 * Define a maping between a string representation and a bitmask for use by
 * this module API
 *
 * @wakeup_bit: the WAKEUP_* bitmask assigned to this wakeup source type
 * @name: the string representation of this source (i.e., from device tree)
 */
struct droid_pm_wakeup_source {
    uint32_t wakeup_bit;
    const char *name;
};

/*
 * Map wakeup sources between name (from device tree) and a bitmask
 * NOTE: New WAKEUP_* types should be defined in wakeup_driver.h and mapped to a
 * string here
 */
static struct droid_pm_wakeup_source sources[] = {
    { WAKEUP_CEC,        "CEC" },
    { WAKEUP_IRR,        "IRR" },
    { WAKEUP_KPD,        "KPD" },
    { WAKEUP_UHFR,       "UHFR" },
    { WAKEUP_XPT_PMU,    "XPT_PMU" },
};

struct droid_pm_priv_data;

/* Definition of the bt proc interface */
struct droid_pm_bt_proc_file
{
    const char *name;
    umode_t mode;
    const struct file_operations *fops;
    ssize_t (*read_action_op)(struct file *file, char __user *buf, size_t size, loff_t *ppos);
    ssize_t (*write_action_op)(struct file *file, const char __user * buf, size_t count, loff_t *ppos);
};

static const char *droid_pm_bt_proc_parent_name = "bluetooth";
static const char *droid_pm_bt_proc_subdir_name = "sleep";

struct droid_pm_instance {
    struct list_head            pm_list;
    struct droid_pm_priv_data * pm_parent;
    pid_t                       pm_pid;
    char *                      pm_comm;
    struct eventfd_ctx *        pm_eventfd_ctx;
    bool                        pm_registered;
    bool                        pm_suspend_ack;
    uint32_t                    pm_wakeups_status;  // The wakeups that have been triggered since the
                                                    // last call to droid_pm_wakeup_ack_status().
};

/* Our device driver private data structure */
struct droid_pm_priv_data {
    spinlock_t                  pm_lock;
    struct device_node *        pm_dn;
    struct device_node *        pm_ether_dn;
    struct proc_dir_entry *     pm_bt_proc_parent_dir;
    struct proc_dir_entry *     pm_bt_proc_subdir_dir;
    struct input_dev *          pm_input_dev;
    bool                        pm_sw_lid;
    bool                        pm_wol;
    bool                        pm_full_wol_wakeup_en;
    unsigned long               pm_wol_event_count;
    unsigned int                pm_irqs[MAX_WAKEUP_IRQS];
    uint32_t                    pm_irq_masks[MAX_WAKEUP_IRQS];
    int                         pm_nr_irqs;
    uint32_t                    pm_wakeups_present;  // The wakeups that are owned by this module
    uint32_t                    pm_wakeups_enabled;  // The wakeups that have been enabled
    struct miscdevice           pm_miscdev;          // Miscellaneous device info
    struct mutex                pm_mutex;            // Protect the data of this structure
    struct completion           pm_suspend_complete; // Used to synchronise when to suspend
    struct notifier_block       pm_notifier;         // Standard PM notifier for power management
    struct notifier_block       pm_reboot_notifier;  // Standard PM notifier for shutdown/reboot
    bool                        pm_ready;            // Set when the driver has initialised and been opened
    bool                        pm_suspending;       // Set when we are about to suspend
    bool                        pm_shutdown;         // Set when we are about to shutdown
    struct droid_pm_instance *  pm_local_instance;   // A local instance used for monitoring wakeups
    struct list_head            pm_instance_list;    // HEAD of droid_pm_instance list
};

static struct platform_device *droid_pm_platform_device;
static struct droid_pm_priv_data droid_pm_priv_data;

/*
 * Check which wakeups are present and assigned to this module.
 * Returns result as a bitmask.
 */
static uint32_t droid_pm_wakeup_check_present(struct droid_pm_priv_data *priv)
{
    return priv->pm_wakeups_present;
}

/* Enable/disable a single wakeup IRQ */
static void droid_pm_wakeup_set_enabled(struct droid_pm_priv_data *priv,
                     uint32_t bitmask, unsigned int enabled)
{
    int i;

    /* Don't mess with interrupts we don't own */
    if (!(bitmask && priv->pm_wakeups_present))
        return;

    /* Wakeup is already set? */
    if ((enabled &&  (priv->pm_wakeups_enabled & bitmask)) ||
       (!enabled && !(priv->pm_wakeups_enabled & bitmask))) {
        return;
    }

    for (i = 0; i < priv->pm_nr_irqs; i++) {
        if (priv->pm_irq_masks[i] == bitmask) {
            irq_set_irq_wake(priv->pm_irqs[i], enabled);
            if (enabled) {
                priv->pm_wakeups_enabled |= bitmask;
            }
            else {
                priv->pm_wakeups_enabled &= ~bitmask;
            }
        }
    }
}

static int droid_pm_wakeups_set_enabled(struct droid_pm_priv_data *priv, uint32_t mask,
                  unsigned int enabled)
{
    spin_lock(&priv->pm_lock);
    do {
        int bit = ffs(mask) - 1;
        droid_pm_wakeup_set_enabled(priv, 1 << bit, enabled);
        mask &= ~(1 << bit);
    } while (mask);
    spin_unlock(&priv->pm_lock);

    return 0;
}

/*
 * Program the wakeup mask, enabling/disabling sources according to the
 * value of each field in @enable
 */
static int droid_pm_wakeup_enable(struct droid_pm_priv_data *priv, uint32_t mask)
{
    return droid_pm_wakeups_set_enabled(priv, mask, 1);
}

static int droid_pm_wakeup_disable(struct droid_pm_priv_data *priv, uint32_t mask)
{
    return droid_pm_wakeups_set_enabled(priv, mask, 0);
}

/*
 * Check and clear wakeup statuses, to see which (if any) sources awoke
 * the system. Returns bitmask representing which device(s) woke the system.
 */
static uint32_t droid_pm_wakeup_ack_status(struct droid_pm_instance *instance)
{
    uint32_t ret;
    struct droid_pm_priv_data *priv = instance->pm_parent;
    struct device *dev = priv->pm_miscdev.parent;

    dev_dbg_ratelimited(dev, "%s: wakeup status=0x%08x [pid=%i,comm=%s]\n", __FUNCTION__, instance->pm_wakeups_status, instance->pm_pid, instance->pm_comm);
    spin_lock(&priv->pm_lock);
    ret = instance->pm_wakeups_status;
    instance->pm_wakeups_status = 0;
    spin_unlock(&priv->pm_lock);

    return ret;
}

static irqreturn_t droid_pm_wakeup_irq(int irq, void *data)
{
    struct list_head *ptr;
    struct droid_pm_priv_data *priv = data;
    struct device *dev = priv->pm_miscdev.parent;
    struct droid_pm_instance *entry;
    int i;

    for (i = 0; i < priv->pm_nr_irqs; i++) {
        if (priv->pm_irqs[i] == irq) {
            dev_dbg(dev, "%s: wakeup source=0x%08x\n", __FUNCTION__, priv->pm_irq_masks[i]);
            spin_lock(&priv->pm_lock);
            list_for_each(ptr, &priv->pm_instance_list) {
                entry = list_entry(ptr, struct droid_pm_instance, pm_list);
                entry->pm_wakeups_status |= priv->pm_irq_masks[i];
            }
            spin_unlock(&priv->pm_lock);
            return IRQ_HANDLED;
        }
    }
    return IRQ_NONE;
}

static uint32_t droid_pm_struct_to_mask(wakeup_devices *wakeups)
{
    uint32_t mask=0;

    if (wakeups->ir)        mask |= WAKEUP_IRR;
    if (wakeups->uhf)       mask |= WAKEUP_UHFR;
    if (wakeups->keypad)    mask |= WAKEUP_KPD;
    if (wakeups->cec)       mask |= WAKEUP_CEC;
    if (wakeups->transport) mask |= WAKEUP_XPT_PMU;

    return mask;
}

static int droid_pm_mask_to_struct(uint32_t mask, wakeup_devices *wakeups)
{
    if (mask & WAKEUP_IRR)     wakeups->ir = 1;
    if (mask & WAKEUP_UHFR)    wakeups->uhf = 1;
    if (mask & WAKEUP_KPD)     wakeups->keypad = 1;
    if (mask & WAKEUP_CEC)     wakeups->cec = 1;
    if (mask & WAKEUP_XPT_PMU) wakeups->transport = 1;

    return 0;
}

static void droid_pm_send_power_key_event(struct droid_pm_priv_data *priv)
{
    input_event(priv->pm_input_dev, EV_KEY, KEY_POWER, 1);
    input_event(priv->pm_input_dev, EV_KEY, KEY_POWER, 0);
    input_sync(priv->pm_input_dev);
}

static void droid_pm_send_lid_switch_event(struct droid_pm_priv_data *priv, bool down)
{
    priv->pm_sw_lid = down;
    input_report_switch(priv->pm_input_dev, SW_LID, down);
    input_sync(priv->pm_input_dev);
}

static void droid_pm_signal_event_l(struct droid_pm_priv_data *priv, uint64_t event)
{
    struct list_head *ptr;
    struct droid_pm_instance *entry;
    struct device *dev = priv->pm_miscdev.parent;

    // Send the event for each registered instance...
    list_for_each(ptr, &priv->pm_instance_list) {
        entry = list_entry(ptr, struct droid_pm_instance, pm_list);
        if (entry->pm_registered) {
            uint64_t ctr;
            // Reset the internal counter to 0 before adding in the event flag(s)...
            eventfd_ctx_read(entry->pm_eventfd_ctx, true, &ctr);
            eventfd_signal(entry->pm_eventfd_ctx, event);
        }
    }
    if (priv->pm_wol) {
        if (event == DROID_PM_EVENT_RESUMED_WAKEUP && priv->pm_full_wol_wakeup_en) {
            droid_pm_send_power_key_event(priv);
            dev_info(dev, "%s POWER event sent\n", __FUNCTION__);
        }
        priv->pm_wol = false;
    }
}

static void droid_pm_clear_suspend_l(struct droid_pm_priv_data *priv)
{
    struct list_head *ptr;
    struct droid_pm_instance *entry;

    // Clear the suspend ack flag for each instance...
    list_for_each(ptr, &priv->pm_instance_list) {
        entry = list_entry(ptr, struct droid_pm_instance, pm_list);
        if (entry->pm_registered) {
            entry->pm_suspend_ack = false;
        }
    }
}

static bool droid_pm_can_we_now_suspend_l(struct droid_pm_priv_data *priv)
{
    struct list_head *ptr;
    struct droid_pm_instance *entry;
    struct device *dev = priv->pm_miscdev.parent;
    int i = 0;
    bool suspend = true;

    // Check to see whether all clients have acknowledged their intent to suspend...
    list_for_each(ptr, &priv->pm_instance_list) {
        entry = list_entry(ptr, struct droid_pm_instance, pm_list);
        dev_dbg(dev, "%s: Checking instance %d registered=%d, suspend ioctl issued=%d\n", __FUNCTION__,
                i, entry->pm_registered, entry->pm_suspend_ack);
        if (entry->pm_registered && !entry->pm_suspend_ack) {
            suspend = false;
        }
        i++;
    }
    return suspend;
}

// Called by the kernel prior to suspending and freezing user-space tasks
static int droid_pm_prepare_suspend(struct droid_pm_priv_data *priv)
{
    int ret = 0;
    struct device *dev = priv->pm_miscdev.parent;
    long timeout = msecs_to_jiffies(suspend_timeout_ms);

    mutex_lock(&priv->pm_mutex);

    if (priv->pm_ready) {
        dev_dbg(dev, "%s: setting suspend flag...\n", __FUNCTION__);
        priv->pm_suspending = true;
        droid_pm_signal_event_l(priv, DROID_PM_EVENT_SUSPENDING);
        reinit_completion(&priv->pm_suspend_complete);

        /* wait for the acknowledgement from user-space that we can now finish suspending... */
        dev_dbg(dev, "%s: Waiting for acknowledgement from user-space...\n", __FUNCTION__);
        mutex_unlock(&priv->pm_mutex);
        ret = wait_for_completion_interruptible_timeout(&priv->pm_suspend_complete, timeout);
        droid_pm_wakeup_ack_status(priv->pm_local_instance);    // Ensure wakeups are cleared
        mutex_lock(&priv->pm_mutex);

        if (ret == 0) {
            // If we timed out, then don't suspend as user-space is not ready!
            dev_warn(dev, "%s: suspend timed out!\n", __FUNCTION__);
            priv->pm_suspending = false;
            ret = -ETIME;
        }
        else if (ret < 0) {
            dev_err(dev, "%s: suspend wait for completion failed [ret=%d]!\n", __FUNCTION__, ret);
            priv->pm_suspending = false;
            ret = -EAGAIN;
        }
        else {
            ret = 0;
        }
    }
    mutex_unlock(&priv->pm_mutex);
    return ret;
}

static bool droid_pm_check_wol_l(struct droid_pm_priv_data *priv)
{
    struct device *dev = priv->pm_miscdev.parent;
    struct platform_device *etherpdev;
    struct device *etherdev;
    struct list_head *ptr;
    struct droid_pm_instance *entry;

    if (priv->pm_ether_dn != NULL) {
        etherpdev = of_find_device_by_node(priv->pm_ether_dn);
        if (etherpdev != NULL) {
            etherdev = &etherpdev->dev;
#ifdef CONFIG_PM_SLEEP
            if ((etherdev != NULL) && (etherdev->power.wakeup != NULL) && (etherdev->power.wakeup->event_count != priv->pm_wol_event_count)) {
                priv->pm_wol_event_count = etherdev->power.wakeup->event_count;
                if (device_can_wakeup(etherdev)) {
                    if (priv->pm_full_wol_wakeup_en) {
                        spin_lock(&priv->pm_lock);
                        list_for_each(ptr, &priv->pm_instance_list) {
                            entry = list_entry(ptr, struct droid_pm_instance, pm_list);
                            /*Consider adding WOL to wakeup sources in wakeup_driver.h and DT*/
                            /*for now, mimic WOL event as one of the wakeup sources */
                            entry->pm_wakeups_status |= WAKEUP_KPD;
                            dev_dbg(dev, "%s entry [pid=%i,comm=%s], status: 0x%x\n", __FUNCTION__,
                                entry->pm_pid, entry->pm_comm, entry->pm_wakeups_status);
                        }
                        spin_unlock(&priv->pm_lock);
                    }
                    priv->pm_wol = true;
                }
            }
#else
            priv->pm_wol = false;
#endif
        }
    }
    return priv->pm_wol;
}

static void droid_pm_complete_resume(struct droid_pm_priv_data *priv)
{
    struct device *dev = priv->pm_miscdev.parent;
    uint64_t event = DROID_PM_EVENT_RESUMED;

    mutex_lock(&priv->pm_mutex);
    dev_dbg(dev, "%s: Clearing suspend flag...\n", __FUNCTION__);
    priv->pm_suspending = false;

    // If we woke up due to a valid wakeup source, then set this event...
    if (droid_pm_wakeup_ack_status(priv->pm_local_instance)) {
        event = DROID_PM_EVENT_RESUMED_WAKEUP;
        droid_pm_send_lid_switch_event(priv, false);
    }
    else if (droid_pm_check_wol_l(priv)) {
        if (priv->pm_full_wol_wakeup_en) {
            event = DROID_PM_EVENT_RESUMED_WAKEUP;
            droid_pm_send_lid_switch_event(priv, false);
        }
        else {
            event = DROID_PM_EVENT_RESUMED_PARTIAL;
        }
    }
    droid_pm_signal_event_l(priv, event);
    mutex_unlock(&priv->pm_mutex);
}

static int droid_pm_notifier(struct notifier_block *notifier, unsigned long droid_pm_event, void *unused)
{
    int ret = NOTIFY_DONE;
    struct droid_pm_priv_data *priv = container_of(notifier, struct droid_pm_priv_data, pm_notifier);
    struct device *dev = priv->pm_miscdev.parent;

    dev_dbg(dev, "%s: PM event %lu\n", __FUNCTION__, droid_pm_event);

    switch (droid_pm_event) {
    case PM_HIBERNATION_PREPARE:
    case PM_SUSPEND_PREPARE:
        ret = (droid_pm_prepare_suspend(priv) == 0) ? NOTIFY_DONE : NOTIFY_BAD;
        break;
    case PM_POST_HIBERNATION:
    case PM_POST_SUSPEND:
    case PM_POST_RESTORE:
        droid_pm_complete_resume(priv);
        break;
    case PM_RESTORE_PREPARE:
        break;
    default:
        break;
    }
    return ret;
}

static int register_droid_pm_notifier(struct droid_pm_priv_data *priv)
{
    priv->pm_notifier.notifier_call = droid_pm_notifier;
    return register_pm_notifier(&priv->pm_notifier);
}

static int unregister_droid_pm_notifier(struct droid_pm_priv_data *priv)
{
    return unregister_pm_notifier(&priv->pm_notifier);
}

static long droid_pm_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{
    int result = 0;
    struct droid_pm_instance *instance = file->private_data;
    struct droid_pm_priv_data *priv = instance->pm_parent;
    struct device *dev = priv->pm_miscdev.parent;

    switch (cmd) {
        case BRCM_IOCTL_WAKEUP_ENABLE: {
            wakeup_devices wakeups;
            uint32_t mask;
            if (copy_from_user(&wakeups, (void*)arg, sizeof(wakeups))) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_WAKEUP_ENABLE: copy_from_user failed!\n", __FUNCTION__);
                break;
            }
            mask = droid_pm_struct_to_mask(&wakeups);
            result = droid_pm_wakeup_enable(priv, mask);
        } break;

        case BRCM_IOCTL_WAKEUP_DISABLE: {
            wakeup_devices wakeups;
            uint32_t mask;
            if (copy_from_user(&wakeups, (void*)arg, sizeof(wakeups))) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_WAKEUP_DISABLE: copy_from_user failed [pid=%i,comm=%s]!\n", __FUNCTION__,
                        instance->pm_pid, instance->pm_comm);
                break;
            }
            mask = droid_pm_struct_to_mask(&wakeups);
            result = droid_pm_wakeup_disable(priv, mask);
        } break;

        case BRCM_IOCTL_WAKEUP_ACK_STATUS: {
            wakeup_devices wakeups;
            uint32_t mask = 0;

            mask = droid_pm_wakeup_ack_status(instance);
            memset(&wakeups, 0, sizeof(wakeups));
            droid_pm_mask_to_struct(mask, &wakeups);

            if (copy_to_user((void*)arg, &wakeups, sizeof(wakeups))) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_WAKEUP_ACK_STATUS: copy_to_user failed [pid=%i,comm=%s]!\n", __FUNCTION__,
                        instance->pm_pid, instance->pm_comm);
            }
        } break;

        case BRCM_IOCTL_WAKEUP_CHECK_PRESENT: {
            wakeup_devices wakeups;
            uint32_t mask = 0;

            mask = droid_pm_wakeup_check_present(priv);
            memset(&wakeups, 0, sizeof(wakeups));
            droid_pm_mask_to_struct(mask, &wakeups);

            if (copy_to_user((void*)arg, &wakeups, sizeof(wakeups))) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_WAKEUP_CHECK_PRESENT: copy_to_user failed [pid=%i,comm=%s]!\n", __FUNCTION__,
                        instance->pm_pid, instance->pm_comm);
            }
        } break;

        case BRCM_IOCTL_REGISTER_EVENTS: {
            int eventfd;
            long r;

            r = get_user(eventfd, (int __user *)arg);
            if (r < 0) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_REGISTER_EVENTS: get_user failed [pid=%i,comm=%s]!\n", __FUNCTION__,
                        instance->pm_pid, instance->pm_comm);
                break;
            }

            if (eventfd <= 0) {
                result = -EINVAL;
                dev_err(dev, "%s: BRCM_IOCTL_REGISTER_EVENTS: invalid event fd=%i provided [pid=%i,comm=%s]!\n", __FUNCTION__,
                        eventfd, instance->pm_pid, instance->pm_comm);
                break;
            }

            dev_dbg(dev, "%s: BRCM_IOCTL_REGISTER_EVENTS: eventfd=%i [pid=%i,comm=%s]\n", __FUNCTION__,
                    eventfd, instance->pm_pid, instance->pm_comm);
            instance->pm_eventfd_ctx = eventfd_ctx_fdget(eventfd);
            if (instance->pm_eventfd_ctx == NULL) {
                dev_dbg(dev, "%s: BRCM_IOCTL_REGISTER_EVENTS: could not get eventfd context eventfd=%i [pid=%i,comm=%s]\n", __FUNCTION__,
                        eventfd, instance->pm_pid, instance->pm_comm);
                result = -EINVAL;
            }
            else {
                instance->pm_registered = true;
            }
        } break;

        case BRCM_IOCTL_UNREGISTER_EVENTS: {
            dev_dbg(dev, "%s: BRCM_IOCTL_UNREGISTER_EVENTS: [pid=%i,comm=%s]\n", __FUNCTION__,
                    instance->pm_pid, instance->pm_comm);

            if (instance->pm_registered) {
                if (instance->pm_eventfd_ctx) {
                    eventfd_ctx_put(instance->pm_eventfd_ctx);
                    instance->pm_eventfd_ctx = NULL;
                }
                instance->pm_registered = false;
            }
            else {
                result = -EINVAL;
                dev_warn(dev, "%s: BRCM_IOCTL_UNREGISTER_EVENTS: not registered [pid=%i,comm=%s]\n", __FUNCTION__,
                         instance->pm_pid, instance->pm_comm);
            }
        } break;

        // Returns: -EAGAIN if we need to try and suspend again because the kernel has already resumed
        case BRCM_IOCTL_SET_SUSPEND_ACK: {
            mutex_lock(&priv->pm_mutex);
            if (priv->pm_suspending || priv->pm_shutdown) {
                dev_dbg(dev, "%s: BRCM_IOCTL_SET_SUSPEND_ACK: signalling ready to %s [pid=%i,comm=%s]\n", __FUNCTION__,
                        priv->pm_suspending ? "suspending" : "shutdown",
                        instance->pm_pid, instance->pm_comm);

                if (instance->pm_registered) {
                    instance->pm_suspend_ack = true;
                }

                // Check to see whether all clients have acknowledged their intent to suspend...
                if (droid_pm_can_we_now_suspend_l(priv)) {
                    // Now signal to let the kernel suspend...
                    complete(&priv->pm_suspend_complete);
                    droid_pm_clear_suspend_l(priv);
                }
            }
            else {
                dev_warn(dev, "%s: BRCM_IOCTL_SET_SUSPEND_ACK: ignoring as PM is not suspending or shutting down [pid=%i,comm=%s]!\n", __FUNCTION__,
                         instance->pm_pid, instance->pm_comm);
                result = -EAGAIN;
            }
            mutex_unlock(&priv->pm_mutex);
        } break;

        case BRCM_IOCTL_SET_INTERACTIVE: {
            int on;
            long r;
            uint64_t event;

            r = get_user(on, (int __user *)arg);
            if (r < 0) {
                result = -EFAULT;
                dev_err(dev, "%s: BRCM_IOCTL_SET_INTERACTIVE: get_user failed [pid=%i,comm=%s]!\n", __FUNCTION__,
                        instance->pm_pid, instance->pm_comm);
                break;
            }

            dev_dbg(dev, "%s: BRCM_IOCTL_SET_INTERACTIVE: signalling interactive %s event [pid=%i,comm=%s]\n", __FUNCTION__,
                    on ? "ON" : "OFF", instance->pm_pid, instance->pm_comm);

            // Signal wakeup status
            event = DROID_PM_EVENT_INTERACTIVE_OFF;
            if (on) {
                event = DROID_PM_EVENT_INTERACTIVE_ON;
            }
            mutex_lock(&priv->pm_mutex);
            droid_pm_signal_event_l(priv, event);
            mutex_unlock(&priv->pm_mutex);
        } break;

        default: {
            result = -ENOSYS;
        } break;
    }
    return result;
}

static struct droid_pm_instance *droid_pm_create_instance_l(struct droid_pm_priv_data *priv)
{
    struct droid_pm_instance *instance;

    instance = kzalloc(sizeof(*instance), GFP_KERNEL);
    if (instance != NULL) {
        instance->pm_parent = priv;
        instance->pm_pid = current->pid;
        instance->pm_comm = kstrdup(current->comm, GFP_KERNEL);
        instance->pm_eventfd_ctx = NULL;
        list_add_tail(&instance->pm_list, &priv->pm_instance_list);
    }
    return instance;
}

static int droid_pm_open(struct inode *inode, struct file *file)
{
    int ret;
    struct droid_pm_instance *instance;
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    struct device *dev = priv->pm_miscdev.parent;

    mutex_lock(&priv->pm_mutex);
    dev_dbg(dev, "%s: entered: [pid=%i,comm=%s]\n", __FUNCTION__, current->pid, current->comm);
    instance = droid_pm_create_instance_l(priv);
    if (instance == NULL) {
        dev_err(dev, "%s: cannot allocate memory!\n", __FUNCTION__);
        ret = -ENOMEM;
    }
    else {
        instance->pm_wakeups_status = priv->pm_local_instance->pm_wakeups_status;
        priv->pm_ready = true;
        file->private_data = instance;
        ret = 0;
    }
    mutex_unlock(&priv->pm_mutex);
    return ret;
}

static void droid_pm_destroy_instance_l(struct droid_pm_instance *instance)
{
    kfree(instance->pm_comm);
    list_del(&instance->pm_list);
    kfree(instance);
}

static int droid_pm_close(struct inode *inode, struct file *file)
{
    struct droid_pm_instance *instance = file->private_data;
    struct droid_pm_priv_data *priv = instance->pm_parent;
    struct device *dev = priv->pm_miscdev.parent;

    mutex_lock(&priv->pm_mutex);
    dev_dbg(dev, "%s: entered: [pid=%i,comm=%s]\n", __FUNCTION__, instance->pm_pid, current->comm);
    droid_pm_destroy_instance_l(instance);
    if (list_empty(&priv->pm_instance_list)) {
        priv->pm_ready = false;
    }
    mutex_unlock(&priv->pm_mutex);
    return 0;
}

static int droid_pm_reboot_shutdown(struct notifier_block *notifier, unsigned long action, void *data)
{
    int ret = NOTIFY_DONE;
    struct droid_pm_priv_data *priv = container_of(notifier, struct droid_pm_priv_data, pm_reboot_notifier);
    struct device *dev = priv->pm_miscdev.parent;

    dev_dbg(dev, "%s: action %lu\n", __FUNCTION__, action);

    if (action == SYS_POWER_OFF) {
        int rc;
        long timeout = msecs_to_jiffies(suspend_timeout_ms);

        // Don't unlock so as to prevent further operations occuring (e.g. like poll)...
        mutex_lock(&priv->pm_mutex);
        priv->pm_shutdown = true;
        reinit_completion(&priv->pm_suspend_complete);
        droid_pm_signal_event_l(priv, DROID_PM_EVENT_SHUTDOWN);
        mutex_unlock(&priv->pm_mutex);

        dev_dbg(dev, "%s: Waiting for acknowledgement from user-space...\n", __FUNCTION__);
        rc = wait_for_completion_interruptible_timeout(&priv->pm_suspend_complete, timeout);

        mutex_lock(&priv->pm_mutex);
        if (rc == 0) {
            // If we timed out, then just shutdown anyway...
            dev_warn(dev, "%s: shutdown timed out!\n", __FUNCTION__);
        }
        else if (rc < 0) {
            dev_err(dev, "%s: shutdown wait for completion failed [rc=%d]!\n", __FUNCTION__, rc);
            priv->pm_shutdown = false;
            ret = NOTIFY_BAD;
        }
        else {
            dev_dbg(dev, "%s: Finished.\n", __FUNCTION__);
        }
        mutex_unlock(&priv->pm_mutex);
    }
    return ret;
}

static int droid_pm_init_wol(struct platform_device *pdev)
{
    struct droid_pm_priv_data *priv = platform_get_drvdata(pdev);
    struct device *dev = &pdev->dev;
    struct device_node *dn;
    int ret = 0;

    priv->pm_full_wol_wakeup_en = false;
    priv->pm_wol = false;
    priv->pm_wol_event_count = 0;

    /* Request the WOL interrupt (shared with gphy driver) */
    dn = of_find_node_by_name(NULL, "ethernet");
    if (!dn) {
        dev_err(dev, "could not find Ethernet node\n");
        ret = -ENXIO;
    }
    else {
        /* Allocate and register an input device for spoofing KEY_POWER event */
        priv->pm_input_dev = input_allocate_device();
        if (!priv->pm_input_dev) {
            dev_err(dev, "Unable to allocate input dev for WOL, error %d\n", ret);
            ret = -ENODEV;
        }
        else {
            set_bit(EV_KEY, priv->pm_input_dev->evbit);
            set_bit(EV_SW, priv->pm_input_dev->evbit);
            set_bit(KEY_POWER, priv->pm_input_dev->keybit);
            set_bit(SW_LID, priv->pm_input_dev->swbit);

            priv->pm_input_dev->name = pdev->name;
            priv->pm_input_dev->id.bustype = BUS_VIRTUAL;
            priv->pm_input_dev->dev.parent = &pdev->dev;

            ret = input_register_device(priv->pm_input_dev);
            if (ret) {
                dev_err(dev, "Unable to register input dev for WOL, error %d\n", ret);
                input_free_device(priv->pm_input_dev);
            }
        }
    }
    priv->pm_ether_dn = dn;
    return ret;
}

static void droid_pm_deinit_wol(struct droid_pm_priv_data *priv)
{
    if (priv->pm_input_dev) {
        input_unregister_device(priv->pm_input_dev);
        input_free_device(priv->pm_input_dev);
    }
    if (priv->pm_ether_dn != NULL) {
        of_node_put(priv->pm_ether_dn);
    }
}


/* BT /proc entries to allow the BT user-space code to signal to the PM driver
   when it is entering low-power mode and when BT_WAKE is asserted/deasserted. */
static ssize_t droid_pm_bt_read_action(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    static const char in_message[] = "Usage:\n"
        "\"echo '0' > %s\" : Deassert BT_WAKE signal\n"
        "\"echo '1' > %s\" : Assert BT_WAKE signal\n"
        "NOTE: This is used internally by libbt and the droid_pm kernel driver.\n";
    static char out_message[sizeof(in_message) + 32];
    struct droid_pm_bt_proc_file *pfile = PDE_DATA(file_inode(file));

    snprintf(out_message, sizeof(out_message)-1, in_message, pfile->name, pfile->name);
    return simple_read_from_buffer(buf, size, ppos, out_message, sizeof(out_message));
}

static ssize_t
droid_pm_bt_proc_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    int ret = 0;
    struct droid_pm_priv_data *priv = proc_get_parent_data(file_inode(file));
    struct device *dev = priv->pm_miscdev.parent;
    struct droid_pm_bt_proc_file *pfile = PDE_DATA(file_inode(file));

    if (pfile->read_action_op) {
        ret = (*pfile->read_action_op)(file, buf, size, ppos);
        if (ret < 0) {
            dev_err(dev, "%s: Failed to read from proc file %s [ret=%d]!\n", __FUNCTION__, pfile->name, ret);
        }
    }
    return ret;
}

static ssize_t droid_pm_bt_write_action(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
    struct droid_pm_priv_data *priv = proc_get_parent_data(file_inode(file));
    struct device *dev = priv->pm_miscdev.parent;
    struct droid_pm_bt_proc_file *pfile = PDE_DATA(file_inode(file));
    uint64_t event;
    char action;

    /* The first byte holds the action */
    if (copy_from_user(&action, buf, 1)) {
        return -EFAULT;
    }

    switch (action) {
        case '0': // Deassert BT_WAKE signal
            event = DROID_PM_EVENT_BT_WAKE_OFF;
            break;
        case '1': // Assert BT_WAKE signal
            event = DROID_PM_EVENT_BT_WAKE_ON;
            break;
        default:
            dev_err(dev, "%s: Invalid %s action=%c!\n", __FUNCTION__, pfile->name, action);
            return -EINVAL;
    }
    dev_dbg(dev, "%s: %s action=%c\n", __FUNCTION__, pfile->name, action);
    mutex_lock(&priv->pm_mutex);
    droid_pm_signal_event_l(priv, event);
    mutex_unlock(&priv->pm_mutex);
    return 0;
}

static ssize_t
droid_pm_bt_proc_write(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
    struct droid_pm_bt_proc_file *pfile = PDE_DATA(file_inode(file));
    struct droid_pm_priv_data *priv = proc_get_parent_data(file_inode(file));
    struct device *dev = priv->pm_miscdev.parent;
    ssize_t ret = count;

    if (pfile->write_action_op) {
        ret = (*pfile->write_action_op)(file, buf, count, ppos);
        if (ret < 0) {
            dev_err(dev, "%s: Failed to write to proc file %s [ret=%d]!\n", __FUNCTION__, pfile->name, ret);
        }
        else {
            ret = count;
        }
    }
    return ret;
}

static const struct file_operations droid_pm_bt_proc_fops = {
    .owner          = THIS_MODULE,
    .read           = droid_pm_bt_proc_read,
    .write          = droid_pm_bt_proc_write,
    .llseek         = default_llseek,
};

static const struct droid_pm_bt_proc_file droid_pm_bt_proc_files[] = {
    {
        "lpm",
        S_IRUGO | S_IWUGO,
        &droid_pm_bt_proc_fops,
        NULL,
        NULL,
    },
    {
        "btwrite",
        S_IRUGO | S_IWUGO,
        &droid_pm_bt_proc_fops,
        &droid_pm_bt_read_action,
        &droid_pm_bt_write_action
    },
    {
        NULL,
        0,
        NULL,
        NULL
    }
};

static int droid_pm_init_proc(struct platform_device *pdev)
{
    struct proc_dir_entry *pdentry;
    struct droid_pm_bt_proc_file const *pfile;
    struct droid_pm_priv_data *priv = platform_get_drvdata(pdev);
    struct device *dev = priv->pm_miscdev.parent;
    int ret = 0;

    priv->pm_bt_proc_parent_dir = proc_mkdir_data(droid_pm_bt_proc_parent_name, S_IRUGO | S_IWUGO | S_IXUGO, NULL, priv);

    if (priv->pm_bt_proc_parent_dir) {
        priv->pm_bt_proc_subdir_dir = proc_mkdir_data(droid_pm_bt_proc_subdir_name, S_IRUGO | S_IWUGO | S_IXUGO, priv->pm_bt_proc_parent_dir, priv);
        if (priv->pm_bt_proc_subdir_dir) {
            for (pfile = droid_pm_bt_proc_files; pfile->name; pfile++) {
                pdentry = proc_create_data(pfile->name, pfile->mode, priv->pm_bt_proc_subdir_dir, pfile->fops, (void *)pfile);
                if (pdentry) {
                    dev_dbg(dev, "%s: Successfully created \"/proc/%s/%s/%s\"\n", __FUNCTION__,
                            droid_pm_bt_proc_parent_name, droid_pm_bt_proc_subdir_name, pfile->name);
                }
                else {
                    dev_err(dev, "%s: Could not create \"/proc/%s/%s/%s\"\n!", __FUNCTION__,
                            droid_pm_bt_proc_parent_name, droid_pm_bt_proc_subdir_name, pfile->name);
                    ret = -EINVAL;
                }
            }
        }
        else {
            dev_err(dev, "%s: Could not create \"/proc/%s/%s\" dir entry!\n", __FUNCTION__,
                    droid_pm_bt_proc_parent_name, droid_pm_bt_proc_subdir_name);
            ret = -EINVAL;
        }
    }
    else {
        dev_err(dev, "%s: Could not create \"/proc/%s\" dir entry!\n", __FUNCTION__, droid_pm_bt_proc_parent_name);
        ret = -EINVAL;
    }
    return ret;
}

void droid_pm_deinit_proc(struct droid_pm_priv_data *priv)
{
    struct droid_pm_bt_proc_file const *pfile;

    if (priv->pm_bt_proc_subdir_dir) {
        for (pfile = droid_pm_bt_proc_files; pfile->name; pfile++) {
            remove_proc_entry(pfile->name, priv->pm_bt_proc_subdir_dir);
        }
        remove_proc_entry(droid_pm_bt_proc_subdir_name, NULL);
        priv->pm_bt_proc_subdir_dir = NULL;
    }
    if (priv->pm_bt_proc_parent_dir) {
        remove_proc_entry(droid_pm_bt_proc_parent_name, NULL);
        priv->pm_bt_proc_parent_dir = NULL;
    }
}

static const struct file_operations droid_pm_fops = {
    .owner          = THIS_MODULE,
    .open           = droid_pm_open,
    .release        = droid_pm_close,
    .unlocked_ioctl = droid_pm_ioctl,
};

static ssize_t map_mem_to_s2_show(struct device *dev,
                  struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", b_map_mem_to_s2);
}

static DEVICE_ATTR_RO(map_mem_to_s2);

static ssize_t full_wol_wakeup_show(struct device *dev,
                  struct device_attribute *attr, char *buf)
{
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    return sprintf(buf, "%d\n", priv->pm_full_wol_wakeup_en);
}

static ssize_t full_wol_wakeup_store(struct device *dev,
                  struct device_attribute *attr, const char *buf, size_t n)
{
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    int value;
    int ret;

    if (sscanf(buf, "0x%x", &value) != 1 && sscanf(buf, "%u", &value) != 1) {
        dev_err(dev, "%s: invalid data\n", __FUNCTION__);
        ret = -EINVAL;
    }
    else {
        if (value) {
            priv->pm_full_wol_wakeup_en = true;
        }
        else {
            priv->pm_full_wol_wakeup_en = false;
        }
        ret = n;
    }
    return ret;
}

static DEVICE_ATTR_RW(full_wol_wakeup);

static ssize_t sw_lid_show(struct device *dev,
                  struct device_attribute *attr, char *buf)
{
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    return sprintf(buf, "%d [%s]\n", priv->pm_sw_lid, priv->pm_sw_lid ? "down" : "up");
}

static ssize_t sw_lid_store(struct device *dev,
                  struct device_attribute *attr, const char *buf, size_t n)
{
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    int value;
    int ret;

    if (sscanf(buf, "0x%x", &value) != 1 && sscanf(buf, "%u", &value) != 1) {
        dev_err(dev, "%s: invalid data\n", __FUNCTION__);
        ret = -EINVAL;
    }
    else {
        droid_pm_send_lid_switch_event(priv, !!value);
        ret = n;
    }
    return ret;
}

static DEVICE_ATTR_RW(sw_lid);

static struct attribute *dev_attrs[] = {
    &dev_attr_map_mem_to_s2.attr,
    &dev_attr_full_wol_wakeup.attr,
    &dev_attr_sw_lid.attr,
    NULL,
};

static struct attribute_group dev_attr_group = {
    .attrs = dev_attrs,
};

static int __init droid_pm_probe(struct platform_device *pdev)
{
    struct droid_pm_priv_data *priv = &droid_pm_priv_data;
    struct device *dev = &pdev->dev;
    struct device_node *dn;
    struct resource *resources;
    int ret, i;

    dev_dbg(dev, "Probing...\n");

    spin_lock_init(&priv->pm_lock);
    mutex_init(&priv->pm_mutex);
    INIT_LIST_HEAD(&priv->pm_instance_list);
    init_completion(&priv->pm_suspend_complete);
    priv->pm_suspending = false;
    priv->pm_shutdown = false;

    /* Sanity check on the module parameters... */
    if (suspend_timeout_ms <= 0) {
        suspend_timeout_ms = SUSPEND_TIMEOUT_MS;
    }

    dn = of_find_node_by_name(NULL, "nexus-wakeups");
    if (!dn) {
        dev_err(dev, "could not find Nexus wakeup node\n");
        return -ENXIO;
    }
    priv->pm_dn = dn;

    resources = kzalloc(sizeof(*resources) * MAX_WAKEUP_IRQS, GFP_KERNEL);
    if (!resources) {
        dev_err(dev, "could not allocate memory for wakeup IRQ resources\n");
        ret = -ENOMEM;
        goto out_free;
    }

    ret = of_irq_to_resource_table(dn, resources, MAX_WAKEUP_IRQS);
    if (ret < 0)
        goto out_free;

    if (ret == 0) {
        dev_err(dev, "no IRQs found\n");
        ret = -ENXIO;
        goto out_free;
    }

    priv->pm_nr_irqs = ret;

    ret = register_droid_pm_notifier(priv);
    if (ret) {
        dev_err(dev, "register_droid_pm_notifier failed [ret=%d]!\n", ret);
        goto out_free;
    }
    else {
        priv->pm_miscdev.minor = MISC_DYNAMIC_MINOR;
        priv->pm_miscdev.name = b_devname;
        priv->pm_miscdev.fops = &droid_pm_fops;
        priv->pm_miscdev.parent = dev;

        ret = misc_register(&priv->pm_miscdev);
        if (ret) {
            dev_err(dev, "misc_register of " DROID_PM_DRV_NAME " failed [ret=%d]!\n", ret);
            unregister_droid_pm_notifier(priv);
            goto out_free;
        }
        else {
            platform_set_drvdata(pdev, priv);
            priv->pm_reboot_notifier.notifier_call = droid_pm_reboot_shutdown;
            register_reboot_notifier(&priv->pm_reboot_notifier);

            ret = sysfs_create_group(&dev->kobj, &dev_attr_group);
            if (ret) {
                dev_err(dev, "failed to create attribute group [ret=%d]!\n", ret);
                goto out_free;
            }
            priv->pm_local_instance = droid_pm_create_instance_l(priv);
            if (priv->pm_local_instance == NULL) {
                dev_err(dev, "%s: cannot allocate memory for local instance!\n", __FUNCTION__);
                ret = -ENOMEM;
                goto out_free;
            }
            priv->pm_local_instance->pm_wakeups_status = 0;

            for (i = 0; i < priv->pm_nr_irqs; i++) {
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
                    dev_err(dev, "no match for IRQ '%s'\n", resources[i].name);
                    continue;
                }

                priv->pm_irqs[i] = resources[i].start;
                /* Save IRQ info . Save this first so we
                   can get correct wakeup status in S5.
                 */
                priv->pm_irq_masks[i] = mask;
                priv->pm_wakeups_present |= mask;

                ret = request_irq(priv->pm_irqs[i], droid_pm_wakeup_irq, 0, DROID_PM_DRV_NAME, priv);

                if (ret) {
                    dev_err(dev, "request_irq failed for '%s'\n", resources[i].name);
                    priv->pm_irq_masks[i] = 0;
                    priv->pm_wakeups_present &= ~mask;
                }
            }

            if (droid_pm_init_wol(pdev)) {
                dev_warn(dev, "WoLAN is not available due to init failure!\n");
            }

            if (droid_pm_init_proc(pdev)) {
                dev_warn(dev, "Cannot initialise /proc interface!\n");
            }
        }
    }
    kfree(resources);
    dev_dbg(dev, "Initialization complete\n");
    return ret;

out_free:
    if (priv->pm_local_instance != NULL) {
        droid_pm_destroy_instance_l(priv->pm_local_instance);
        priv->pm_local_instance = NULL;
    }
    if (resources) {
        /* Free all IRQs we grabbed */
        for (i = 0; i < priv->pm_nr_irqs; i++)
            if (priv->pm_irq_masks[i]) {
                if(priv->pm_wakeups_enabled & priv->pm_irq_masks[i]) {
                    irq_set_irq_wake(priv->pm_irqs[i], 0);
                }
                free_irq(resources[i].start, priv);
            }
        kfree(resources);
    }

    droid_pm_deinit_wol(priv);
    droid_pm_deinit_proc(priv);
    of_node_put(dn);

    return ret;
}

static int __exit droid_pm_remove(struct platform_device *pdev)
{
    struct droid_pm_priv_data *priv = platform_get_drvdata(pdev);
    struct device *dev = &pdev->dev;
    int i;

    dev_dbg(dev, "%s: called\n", __FUNCTION__);
    mutex_lock(&priv->pm_mutex);
    if (priv->pm_local_instance != NULL) {
        droid_pm_destroy_instance_l(priv->pm_local_instance);
    }
    sysfs_remove_group(&dev->kobj, &dev_attr_group);
    unregister_reboot_notifier(&priv->pm_reboot_notifier);
    unregister_droid_pm_notifier(priv);
    misc_deregister(&priv->pm_miscdev);

    for (i = 0; i < priv->pm_nr_irqs; i++)
        if (priv->pm_irq_masks[i]) {
            if(priv->pm_wakeups_enabled & priv->pm_irq_masks[i]) {
                irq_set_irq_wake(priv->pm_irqs[i], 0);
            }
            free_irq(priv->pm_irqs[i], priv);
        }
    droid_pm_deinit_wol(priv);
    of_node_put(priv->pm_dn);
    mutex_unlock(&priv->pm_mutex);
    dev_dbg(dev, "%s: Cleanup complete\n", __FUNCTION__);
    return 0;
}

static void droid_pm_shutdown(struct platform_device *pdev)
{
    struct droid_pm_priv_data *priv = platform_get_drvdata(pdev);
    dev_dbg(&pdev->dev, "%s: entered\n", __FUNCTION__);
    // Prevent further operations occuring (e.g. like poll)...
    mutex_lock(&priv->pm_mutex);
}

static struct platform_driver droid_pm_driver = {
    .driver = {
        .owner = THIS_MODULE,
        .name = DROID_PM_DRV_NAME,
    },
    .remove = droid_pm_remove,
    .shutdown = droid_pm_shutdown,
};

static int __init droid_pm_init(void)
{
    int ret;

    pr_info("Broadcom Android Power Management Driver Version %s\n", DRV_VERSION);

    droid_pm_platform_device = platform_device_register_simple(DROID_PM_DRV_NAME, -1, NULL, 0);
    if (IS_ERR(droid_pm_platform_device)) {
        ret = PTR_ERR(droid_pm_platform_device);
    }
    else {
        ret = platform_driver_probe(&droid_pm_driver, droid_pm_probe);
        if (ret) {
            pr_err("Could not probe for " DROID_PM_DRV_NAME " [ret=%d]!\n", ret);
            platform_device_unregister(droid_pm_platform_device);
        }
    }
    return ret;
}

static void __exit droid_pm_exit(void)
{
    platform_driver_unregister(&droid_pm_driver);
    platform_device_unregister(droid_pm_platform_device);
    pr_info(DROID_PM_DRV_NAME " unloaded\n");
}

module_init(droid_pm_init);
module_exit(droid_pm_exit);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom power management driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
