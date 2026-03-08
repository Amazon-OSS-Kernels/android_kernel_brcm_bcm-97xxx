/*
 *  trip_step_wise.c - A simple thermal throttling governor
 *
 *  Copyright (C) 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved
 *  Author: Akwasi Boateng <boatenga@amazon.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 *
 */

#include <linux/thermal.h>
#include <linux/module.h>
#include "thermal_core.h"

static DEFINE_MUTEX(trip_step_wise_lock);

/**
 * trip_step_wise_throttle
 * @tz - thermal_zone_device
 * @trip - the trip point
 *
 */
static int trip_step_wise_throttle(struct thermal_zone_device *tz, int trip)
{
	long trip_temp;
	struct thermal_instance *tz_instance;
	struct thermal_cooling_device *cdev;
	unsigned long target = 0;
	char data[32];
	char *envp[] = { data, NULL };
	unsigned long max_state;
	int thermal_state; /* state0 TRIP0 state 1 TRIP1 state2 TRIP2 ... */
	struct thermal_instance* active_instances[THERMAL_MAX_TRIPS];
	unsigned int num_active_instance = 0, i;
	enum thermal_device_mode mode = THERMAL_DEVICE_DISABLED;
	int result;
	mutex_lock(&trip_step_wise_lock);
	mutex_lock(&tz->lock);

	result = tz->ops->get_mode(tz, &mode);
	if ((result) || (mode == THERMAL_DEVICE_DISABLED)) {
		//device disabled, return from here...
		mutex_unlock(&trip_step_wise_lock);
		mutex_unlock(&tz->lock);
		return 0;
	}

	list_for_each_entry(tz_instance, &tz->thermal_instances, tz_node) {
		if (tz_instance->trip != trip) {
			// set thermal_instance::target to THERMAL_NO_TARGET for all tz_instance entries
			// whose trip point is not currently active.
			tz_instance->target = THERMAL_NO_TARGET;
			continue;
		} else {
			// store currently active instances
			active_instances[num_active_instance] = tz_instance;
			num_active_instance++;
			// tz_instance->target will be set below
		}
	}

	if (trip == THERMAL_TRIPS_NONE)
		trip_temp = tz->forced_passive;
	else
		tz->ops->get_trip_temp(tz, trip, &trip_temp);

	for (i = 0; i < num_active_instance; i++) {
		tz_instance = active_instances[i];
		cdev = tz_instance->cdev;

		mutex_lock(&cdev->lock);
		if (tz->temperature >= trip_temp) {
				target = tz_instance->upper;
				thermal_state = trip + 1; /* rising: state1 maps to trip0, etc. */
		} else {
				target = tz_instance->lower;
				thermal_state = trip; /* falling: state1 handled in trip1 */
		}
		cdev->ops->get_max_state(cdev, &max_state);
		target = (target > max_state) ? max_state : target;
		cdev->updated = false;
		// now set the newly requested target to the active thermal_instance
		tz_instance->target = target;
		mutex_unlock(&cdev->lock);
		thermal_cdev_update(cdev);
		snprintf(data, sizeof(data), "THERMAL_STATE=%u", thermal_state);
		kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);
	}
	mutex_unlock(&tz->lock);
	mutex_unlock(&trip_step_wise_lock);
	return 0;
}

static struct thermal_governor thermal_gov_trip_step_wise = {
	.name = "trip_step_wise",
	.throttle = trip_step_wise_throttle,
};

static int __init thermal_gov_trip_step_wise_init(void)
{
	return thermal_register_governor(&thermal_gov_trip_step_wise);
}

static void __exit thermal_gov_trip_step_wise_exit(void)
{
	thermal_unregister_governor(&thermal_gov_trip_step_wise);
}

fs_initcall(thermal_gov_trip_step_wise_init);
module_exit(thermal_gov_trip_step_wise_exit);

MODULE_AUTHOR("Akwasi Boateng");
MODULE_DESCRIPTION("A simple trip level throttling thermal governor");
MODULE_LICENSE("GPL");
