/*
 * virtual_sensor_hotplug_cooling.c - Virtual sensor cpu_hotplug works as cooling device.
 *
 * Copyright 2015 Amazon.com, Inc. or its Affiliates. All rights reserved.
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

#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/cpu_hotplug_cooling.h>
#include <linux/device.h>
#include <linux/thermal.h>
#include <thermal_core.h>

static struct thermal_cooling_device *cdev[NR_CPUS];

static int virtual_sensor_hotplug_cooling_probe(struct platform_device *pdev)
{
	int i;
	int nr_cpus = num_possible_cpus();

	for (i = 1; i < nr_cpus; i++) {
		cdev[i] = cpu_hotplug_cooling_register(i);

		if (IS_ERR(cdev[i])) {
			dev_err(&pdev->dev, "Failed to register cooling device\n");
			return PTR_ERR(cdev[i]);
		}
	}

	platform_set_drvdata(pdev, cdev);

	for (i = 1; i < nr_cpus; i++) {
		dev_info(&pdev->dev, "Cooling device registered: %s\n",	cdev[i]->type);
	}
	return 0;
}

static int virtual_sensor_hotplug_cooling_remove(struct platform_device *pdev)
{
	int i;
	struct thermal_cooling_device **cdev = platform_get_drvdata(pdev);
	int nr_cpus = num_possible_cpus();

	for (i = 1; i < nr_cpus; i++)
		cpu_hotplug_cooling_unregister(cdev[i]);

	return 0;
}

static struct platform_driver virtual_sensor_hotplug_cooling_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "virtual_sensor-cpu-hotplug-cooling",
	},
	.probe = virtual_sensor_hotplug_cooling_probe,
	.remove = virtual_sensor_hotplug_cooling_remove,
};

static struct platform_device virtual_sensor_hotplug_cooling_device = {
	.name = "virtual_sensor-cpu-hotplug-cooling",
	.id = -1,
};

static int __init virtual_sensor_hotplug_cooling_init(void)
{
	int ret;
	ret = platform_driver_register(&virtual_sensor_hotplug_cooling_driver);
	if (ret) {
		pr_err("Unable to register VIRTUAL_SENSOR cpu_hotplug cooling driver (%d)\n", ret);
		return ret;
	}
	ret = platform_device_register(&virtual_sensor_hotplug_cooling_device);
	if (ret) {
		pr_err("Unable to register VIRTUAL_SENSOR cpu_hotplug cooling device (%d)\n", ret);
		return ret;
	}
	return 0;
}

static void __exit virtual_sensor_hotplug_cooling_exit(void)
{
	platform_driver_unregister(&virtual_sensor_hotplug_cooling_driver);
	platform_device_unregister(&virtual_sensor_hotplug_cooling_device);
}

module_init(virtual_sensor_hotplug_cooling_init);
module_exit(virtual_sensor_hotplug_cooling_exit);

MODULE_AUTHOR("Akwasi Boateng <boatenga@amazon.com>");
MODULE_DESCRIPTION("Virtual sensor cpu_hotplug cooling driver");
MODULE_LICENSE("GPL");
