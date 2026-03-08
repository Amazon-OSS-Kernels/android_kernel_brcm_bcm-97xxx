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

#ifndef __WAKEUP_DRIVER_H__
#define __WAKEUP_DRIVER_H__
#include <linux/ioctl.h>

#define WAKEUP_CEC		(1 << 0)
#define WAKEUP_IRR		(1 << 1)
#define WAKEUP_KPD		(1 << 2)
#define WAKEUP_GPIO     (1 << 3)
#define WAKEUP_UHFR		(1 << 4)
#define WAKEUP_XPT_PMU	(1 << 5)



typedef struct wakeup_devices{
    uint32_t ir;
    uint32_t uhf;
    uint32_t keypad;
    uint32_t gpio;
    uint32_t cec;
    uint32_t transport;
    uint32_t timeout;     /* in seconds */
} wakeup_devices;


#define BRCM_IOCTL_WAKEUP_ENABLE            _IOW(102, 1, wakeup_devices)
#define BRCM_IOCTL_WAKEUP_DISABLE           _IOW(102, 2, wakeup_devices)
#define BRCM_IOCTL_WAKEUP_ACK_STATUS        _IOR(102, 3, wakeup_devices)
#define BRCM_IOCTL_WAKEUP_CHECK_PRESENT     _IOR(102, 4, wakeup_devices)

#endif /* __WAKEUP_DRIVER_H__ */
