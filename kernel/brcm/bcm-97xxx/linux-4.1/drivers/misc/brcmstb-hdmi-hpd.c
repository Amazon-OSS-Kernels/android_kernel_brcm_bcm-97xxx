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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/brcmstb/hdmi_hpd_switch.h>

static DEFINE_MUTEX(hpd_switch_lock);
static struct switch_dev hdmi_hpd_switch;

static long brcmstb_hdmi_hpd_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	enum hdmi_state switch_state;

	switch (cmd) {
	case HDMI_HPD_IOCTL_SET_SWITCH:
		if (copy_from_user(&switch_state, (void __user *)arg,
			sizeof(switch_state)))
			return -EFAULT;
		if (switch_state != HDMI_UNPLUGGED &&
		    switch_state != HDMI_CONNECTED) {
			pr_err("%s: invalid switch state (%d)\n", __func__,
				switch_state);
			return -EINVAL;
		}
		mutex_lock(&hpd_switch_lock);
		switch_set_state(&hdmi_hpd_switch, switch_state);
		mutex_unlock(&hpd_switch_lock);
		break;
	default:
		pr_err("%s: invalid command\n", __func__);
		return -ENOIOCTLCMD;
	};

	return 0;
}

#ifdef CONFIG_COMPAT
static long brcmstb_hdmi_hpd_compat_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	return brcmstb_hdmi_hpd_ioctl(file, cmd,
				(unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations brcmstb_hdmi_hpd_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = brcmstb_hdmi_hpd_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= brcmstb_hdmi_hpd_compat_ioctl,
#endif
};

static struct miscdevice brcmstb_hdmi_hpd_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "hdmi_hpd",
	.fops	= &brcmstb_hdmi_hpd_fops,
};

static int __init brcmstb_hdmi_hpd_init(void)
{
	int ret;

	ret = misc_register(&brcmstb_hdmi_hpd_miscdev);
	if (ret) {
		pr_err("%s: misc_register failed\n", __func__);
		return ret;
	}

	hdmi_hpd_switch.name = "hdmi_audio";
	ret = switch_dev_register(&hdmi_hpd_switch);
	if (ret) {
		pr_err("%s: switch_dev_register failed\n", __func__);
		misc_deregister(&brcmstb_hdmi_hpd_miscdev);
		return ret;
	}

	pr_info("brcmstb_hdmi_hpd switch successfully registered\n");
	return 0;
}

static void __exit brcmstb_hdmi_hpd_exit(void)
{
	switch_dev_unregister(&hdmi_hpd_switch);
	misc_deregister(&brcmstb_hdmi_hpd_miscdev);
}

module_init(brcmstb_hdmi_hpd_init);
module_exit(brcmstb_hdmi_hpd_exit);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_LICENSE("GPLv2");
