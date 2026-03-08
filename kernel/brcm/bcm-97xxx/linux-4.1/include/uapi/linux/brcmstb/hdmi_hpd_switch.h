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

#ifndef __HDMI_HPD_SWITCH_H__
#define __HDMI_HPD_SWITCH_H__

#define HDMI_HPD_MAGIC 'h'
#define HDMI_HPD_SET_SWITCH 0x80

#define HDMI_HPD_IOCTL_SET_SWITCH \
	_IOW(HDMI_HPD_MAGIC, HDMI_HPD_SET_SWITCH, enum hdmi_state)

enum hdmi_state {
	HDMI_UNPLUGGED,
	HDMI_CONNECTED
};

#endif /* __HDMI_HPD_SWITCH_H__ */
