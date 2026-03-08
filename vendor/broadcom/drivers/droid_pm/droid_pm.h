/*
 * Copyright (C) 2015 Broadcom
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

#ifndef __DROID_PM_H__
#define __DROID_PM_H__

#include <wakeup/wakeup_driver.h>

#define DROID_PM_DRV_NAME               "droid_pm"

/* Must be the same value as for the wakeup driver... */
#define DROID_PM_IOC_MAGIC              102

/* Power event state definitions... */
#define DROID_PM_EVENT_INTERACTIVE_OFF  1   /* Set when kernel received an interactive OFF event */
#define DROID_PM_EVENT_INTERACTIVE_ON   2   /* Set when kernel received an interactive ON event */
#define DROID_PM_EVENT_RESUMED          3   /* Set if kernel resumed without receiving a wakeup event */
#define DROID_PM_EVENT_RESUMED_WAKEUP   4   /* Set when kernel resumed due to a wakeup event */
#define DROID_PM_EVENT_RESUMED_PARTIAL  5   /* Set when kernel resumed due to a partial wakeup event (e.g. WoL) */
#define DROID_PM_EVENT_SUSPENDING       6   /* Set when kernel is about to suspend */
#define DROID_PM_EVENT_SHUTDOWN         7   /* Set when kernel is about to shutdown */
#define DROID_PM_EVENT_BT_WAKE_OFF      8   /* Set when the Bluetooth device sleeps */
#define DROID_PM_EVENT_BT_WAKE_ON       9   /* Set when the Bluetooth device awakens */

/* Each client that is interested in knowing when the kernel is
   about to suspend so it can perform some last minute actions
   needs to follow the steps below.

   1) Each client needs to open up the droid_pm driver and register
      itself with the driver using the BRCM_IOCTL_REGISTER_EVENTS
      ioctl. The argument passed is an eventfd object that is used
      to provide events from the droid_pm driver to the client.

   2) The client can then use either the poll or select API on the
      eventfd object to block waiting for an event to be set by the
      droid_pm driver.  The client can then use the standard
      "eventfd_read()" API to obtain the actual event flags as defined
      above.

      If a "DROID_PM_EVENT_SUSPENDING" event is set, then the client
      can use this as a trigger to perform the necessary actions such
      as placing the Nexus drivers in to standby.

   3) Once the client has performed its actions, it needs to inform
      the droid_pm driver that it is ready for the kernel to suspend
      or shutdown. This is achieved by issuing the ioctl
      "BRCM_IOCTL_SET_SUSPEND_ACK".

      NOTE: If more than one client has registered their interest in
      the kernel suspending, then each must issue the ioctl
      "BRCM_IOCTL_SET_SUSPEND_ACK" before the kernel can fully suspend
      or shutdown.

   4) After issuing the "BRCM_IOCTL_SET_SUSPEND_ACK" ioctl, each client
      should check whether the kernel successfully suspended or not by
      waiting for the "DROID_PM_EVENT_RESUMED",
      "DROID_PM_EVENT_RESUMED_PARTIAL" or
      "DROID_PM_EVENT_RESUMED_WAKEUP" event to be set by droid_pm.
      The client can simply poll and/or call "eventfd_read()" to obtain
      the event.

      If the client received the "DROID_PM_EVENT_RESUMED_WAKEUP" event,
      then it meant that the kernel received a valid wake-up event and
      the client can then take appropriate action such as disabling WoBLE.

      If the client received the "DROID_PM_EVENT_RESUMED_PARTIAL" event,
      then it meant that the kernel received a valid wake-up event but
      only part of the system should be woken up.  For example, when the
      Wake-On-Lan (WoL) event is received, it is possible for only the
      kernel to be woken and not all of Android.

      If the "DROID_PM_EVENT_RESUMED" event was received instead, it meant
      that the kernel did not fully suspend and this may be due to an
      active wake-lock preventing the kernel from suspending.

      If the kernel failed to fully suspend, then the client should repeat
      steps 2) to 4) again waiting for the kernel to begin suspending again.

   5) The "BRCM_IOCTL_SET_INTERACTIVE" ioctl is called by the Power HAL
      when a "set_interactive" callback is received by it.  This ioctl is
      used by droid_pm to correctly set the "DROID_PM_EVENT_INTERACTIVE_ON"
      or "DROID_PM_EVENT_INTERACTIVE_OFF" event.

   6) Once a client no longer needs to react to the events set by the droid_pm
      driver, it should issue the ioctl "BRCM_IOCTL_UNREGISTER_EVENTS".
*/

/*
   Below is the list of IOCTL's that can be issued to the droid_pm driver
   in addition to the wakeup_driver IOCTL's.
*/
#define BRCM_IOCTL_REGISTER_EVENTS      _IOW(DROID_PM_IOC_MAGIC, 10, int)
#define BRCM_IOCTL_UNREGISTER_EVENTS    _IO( DROID_PM_IOC_MAGIC, 11)
#define BRCM_IOCTL_SET_SUSPEND_ACK      _IO( DROID_PM_IOC_MAGIC, 12)
#define BRCM_IOCTL_SET_INTERACTIVE      _IOW(DROID_PM_IOC_MAGIC, 13, int)

#endif /* __DROID_PM_H__ */
