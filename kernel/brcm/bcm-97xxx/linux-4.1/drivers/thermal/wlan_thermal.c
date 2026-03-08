/*
 * Copyright (C) 2017 Lab126, Inc.  All rights reserved.
 * Author: Venkat Junnuthulla <vjjunnut@amazon.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal_framework.h>
#include <linux/platform_data/bcm_thermal.h>
#include <linux/reboot.h>
#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define WLAN_THERMAL_METRICS_STR_LEN 256
char *wlan_thermal_metrics_prefix = "frankthermal";
extern const char *product_id2;
#endif
extern int get_wifi_tempsense(int chip_id, int *ret_phytemp_ptr);
extern u64 idme_get_usr_flags_value(void);
extern unsigned int idme_get_board_rev(void);

#define MAX_NAME_LENGTH 64
#define DRIVER_NAME "wlan_thermal"
#define DEFAULT_MAX_TEMP_FAILS 10
#define WLAN_MAX_REASONABLE_TEMP 125000
#define USR_FLAGS_PCBA (1<<1) /* IDME usr_flags bit 1 */
static DEFINE_MUTEX(wlan_thermal_lock);
static int pcba; /* if IDME pcba flag is set, use it as flag to disable wifi temp caused reboot */

struct wlan_thermal_data {
	unsigned int wl_id;
	int error_limit_init;
	int error_limit_runtime;
	int fail_count;
	int err_msg_printed;
	int error_limit;
	int wl_module_found;
};

struct wlan_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct bcm_thermal_platform_data *pdata;
	struct thermal_dev *enclosure_therm_fw;
	struct thermal_dev *fconnector_therm_fw;
};

static const struct bcm_thermal_platform_data wlan_thermal_data_default_policy = {
	.num_trips = 1,
	.mode = THERMAL_DEVICE_DISABLED,
	.polling_delay = 3000,
	.trips[0] = {.temp = 30000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = { .type = "pwm-fan", .upper = 1, .lower = 1},},
};

static int wlan_get_temp(struct thermal_dev* therm_fw)
{
	const char *wl_module_name = "wl";
	struct module *wl = NULL;
	struct wlan_thermal_data* wl_data;
	int ret;
	int temp = 0;

#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[WLAN_THERMAL_METRICS_STR_LEN];
#endif
	if (!therm_fw) {
		pr_err("Invalid wlan thermal device\n");
		return temp;
	}
	wl_data = (struct wlan_thermal_data*)therm_fw->dev;

	if (!wl_data->wl_module_found) {
		mutex_lock(&module_mutex);
		wl = find_module(wl_module_name);
		mutex_unlock(&module_mutex);
		if (wl) {
			wl_data->wl_module_found = 1;
			wl_data->error_limit = wl_data->error_limit_init;
			pr_info("error_limit_init = %d\n", wl_data->error_limit);
		}
		/* return zero temperature until wifi kernel module is loaded */
		return temp;
	}

	ret = get_wifi_tempsense(wl_data->wl_id, &temp);
	if (ret ) {
		wl_data->fail_count++;
		temp = 0;
		if( !wl_data->err_msg_printed ) {
			pr_err ("get_wifi_tempsense(wlan-%d) returned error:%d and invalid temp %d \n", wl_data->wl_id, ret, temp);
			wl_data->err_msg_printed = 1;
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, WLAN_THERMAL_METRICS_STR_LEN,"%s:%s:wlan_thermal_error_code_%d=1;CT;1:NR", wlan_thermal_metrics_prefix, product_id2? product_id2: "def", ret);
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		}
	} else {
		temp *= 1000; //convert to milli as framework works on milli
		wl_data->err_msg_printed = 0;
		//framework doesn't handle '-ve' temp everywhere correctly
		if (temp < 0) {
			temp = 0;
		}

		if (temp > WLAN_MAX_REASONABLE_TEMP) {
			wl_data->fail_count++;
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, WLAN_THERMAL_METRICS_STR_LEN,"%s:%s:wlan_thermal_abnormal_temp_read_event=1;CT;1:NR",
			wlan_thermal_metrics_prefix, product_id2? product_id2: "def");
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		} else {
			// it takes up to 45 seconds for the WL driver to get loaded.
			// so we allow a bigger fail limit initially.  But once we get
			// a good value, we reset to a more reasonable limit.
			wl_data->error_limit = wl_data->error_limit_runtime;
			wl_data->fail_count=0;
		}
	}
	if( wl_data->fail_count > wl_data->error_limit && !pcba) {
		pr_err("wlan temp sensor failed\n");
		temp = SENSOR_FAILED_RETURN_TEMP;
	}
	return temp;
}

static struct thermal_dev_ops wlan_temp_sensor_ops = {
	.get_temp = wlan_get_temp,
};

static struct thermal_dev * init_wlan_thermal_dev(struct platform_device *pdev)
{
	struct thermal_dev* therm_fw;
	struct wlan_thermal_data* wl_data;
	struct device_node *node;

	therm_fw = (struct thermal_dev*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if (!therm_fw) {
		return NULL;
	}

	therm_fw->tdp = (struct thermal_dev_params*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev_params), GFP_KERNEL);
	if (!therm_fw->tdp) {
		return NULL;
	}

	therm_fw->name = (char*)devm_kzalloc(&pdev->dev, MAX_NAME_LENGTH, GFP_KERNEL);
	if (!therm_fw->name) {
		return NULL;
	}

	wl_data = (struct wlan_thermal_data*)devm_kzalloc(&pdev->dev, sizeof(struct wlan_thermal_data), GFP_KERNEL);
	if (!wl_data) {
		return NULL;
	}

	node = pdev->dev.of_node;
	of_property_read_u32(node, "wlan-id", &wl_data->wl_id);
	of_property_read_u32(node, "offset", &therm_fw->tdp->offset);
	of_property_read_u32(node, "alpha", &therm_fw->tdp->alpha);
	of_property_read_u32(node, "weight", &therm_fw->tdp->weight);
	of_property_read_u32(node, "error_limit_init", &wl_data->error_limit_init);
	of_property_read_u32(node, "error_limit_runtime", &wl_data->error_limit_runtime);

	therm_fw->dev = (struct device*)wl_data;
	snprintf(therm_fw->name, MAX_NAME_LENGTH, "wlan-%d", wl_data->wl_id);
	therm_fw->dev_ops = &wlan_temp_sensor_ops;

	return therm_fw;
}

static ssize_t wlan_temp_sensor_show_params(struct device *dev,
					    struct device_attribute *devattr,
					    char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct wlan_thermal_zone   *tzone   = thermal->devdata;

	return sprintf(buf, "Enclosure offset=%d alpha=%d weight=%d\nFconnector offset=%d alpha=%d weight=%d\n",
			tzone->enclosure_therm_fw->tdp->offset,
			tzone->enclosure_therm_fw->tdp->alpha,
			tzone->enclosure_therm_fw->tdp->weight,
			tzone->fconnector_therm_fw->tdp->offset,
			tzone->fconnector_therm_fw->tdp->alpha,
			tzone->fconnector_therm_fw->tdp->weight);
}

static ssize_t wlan_temp_sensor_set_params(struct device *dev,
					   struct device_attribute *devattr,
					   const char *buf,
					   size_t count)
{
	struct thermal_dev* therm_fw        = NULL;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct wlan_thermal_zone   *tzone   = thermal->devdata;
	char zone[20];
	char param[20];
	int value = 0;

	if (sscanf(buf, "%s %s %d", zone, param, &value) == 3) {
		if( !strcmp( zone, "enclosure"))
			therm_fw = tzone->enclosure_therm_fw;
		else if( !strcmp( zone, "fconnector"))
			therm_fw = tzone->fconnector_therm_fw;
		else
			return -EINVAL;

		if (!strcmp(param, "offset"))
			therm_fw->tdp->offset = value;
		else if (!strcmp(param, "alpha"))
			therm_fw->tdp->alpha = value;
		else if (!strcmp(param, "weight"))
			therm_fw->tdp->weight = value;
		else
			return -EINVAL;
		return count;
	}
	return -EINVAL;
}

static ssize_t wlan_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t wlan_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &polling_delay) != 1)
		return -EINVAL;
	if (polling_delay < 0)
		return -EINVAL;

	pdata->polling_delay = polling_delay;
	thermal->polling_delay = pdata->polling_delay;
	monitor_thermal_zone(thermal);
	return count;
}

static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, wlan_polling_show, wlan_polling_store);
static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, wlan_temp_sensor_show_params,
                   wlan_temp_sensor_set_params);

static int wlan_thermal_create_sysfs(struct wlan_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s: Failed to create polling attr\n", DRIVER_NAME);

	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s: Failed to create params attr\n", DRIVER_NAME);

	return ret;
}

/****************************Individual thermal zone code starts here*****************************/

static int wlan_match_cdev(struct thermal_cooling_device* cdev,
			   struct trip_t* trip,
			   int* index)
{
	int i;
	if (!strlen(cdev->type))
		return -EINVAL;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++)
		if (!strcmp(cdev->type, trip->cdev[i].type)) {
			*index = i;
			return 0;
		}
	return -ENODEV;
}

static int wlan_cdev_bind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (wlan_match_cdev(cdev, trip, &index))
			continue;

		if (index == -1)
			return -EINVAL;

		cool_dev = &(trip->cdev[index]);
		lower = cool_dev->lower;
		upper =  cool_dev->upper > max_state ? max_state : cool_dev->upper;
		ret = thermal_zone_bind_cooling_device(thermal,
						       i,
						       cdev,
						       upper,
						       lower);
		dev_info(&cdev->device, "%s bind to %d: %d-%s\n", cdev->type,
			 i, ret, ret ? "fail" : "succeed");
	}
	return ret;
}

static int wlan_cdev_unbind(struct thermal_zone_device *thermal,
			    struct thermal_cooling_device *cdev)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (wlan_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int wlan_thermal_get_temp(struct thermal_zone_device *thermal,
				 unsigned long *t)
{
	struct wlan_thermal_zone   *tzone;

	if (!t || !thermal || !thermal->devdata) {
		pr_err("%s: Invalid thermal zone device %p %p",DRIVER_NAME, thermal, thermal->devdata);
		return -EINVAL;
	}
	tzone = thermal->devdata;
	*t = wlan_get_temp(tzone->enclosure_therm_fw);
	return 0;
}

static int wlan_thermal_get_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode *mode)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&wlan_thermal_lock);
	*mode = pdata->mode;
	mutex_unlock(&wlan_thermal_lock);
	return 0;
}

static int wlan_thermal_set_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode mode)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&wlan_thermal_lock);
	pdata->mode = mode;
	if (mode == THERMAL_DEVICE_DISABLED) {
		tzone->tz->polling_delay = 0;
	}
	else {
		// After getting enabled we need to make sure tzone->tz->polling_delay is restored
		// back to currently set polling delay and not zero anymore.
		tzone->tz->polling_delay = pdata->polling_delay;
	}
	schedule_work(&tzone->therm_work);
	mutex_unlock(&wlan_thermal_lock);
	return 0;
}

static int wlan_thermal_get_trip_type(struct thermal_zone_device *thermal,
				      int trip,
				      enum thermal_trip_type *type)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;

	return 0;
}

static int wlan_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *temp)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}

static int wlan_thermal_set_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long temp)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].temp = temp;
	if (trip < (thermal->trips - 1))
		pdata->trips[trip].type = THERMAL_TRIP_ACTIVE;
	else
		pdata->trips[trip].type = THERMAL_TRIP_CRITICAL;
	return 0;
}

static int wlan_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				      unsigned long *temp)
{
	int i;
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int wlan_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *hyst)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;

	return 0;
}
static int wlan_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long hyst)
{
	struct wlan_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static int wlan_thermal_notify(struct thermal_zone_device *thermal,
			       int trip,
			       enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&thermal->device.kobj, KOBJ_CHANGE, envp);

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
	if( type == THERMAL_TRIP_CRITICAL )
		life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_WIFI);
#endif
	return 0;
}

static int wlan_thermal_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	return 0;
}

static struct thermal_zone_device_ops wlan_tz_dev_ops = {
	.bind = wlan_cdev_bind,
	.unbind = wlan_cdev_unbind,
	.get_temp = wlan_thermal_get_temp,
	.get_mode = wlan_thermal_get_mode,
	.set_mode = wlan_thermal_set_mode,
	.set_trips = wlan_thermal_set_trips,
	.get_trip_type = wlan_thermal_get_trip_type,
	.get_trip_temp = wlan_thermal_get_trip_temp,
	.set_trip_temp = wlan_thermal_set_trip_temp,
	.get_crit_temp = wlan_thermal_get_crit_temp,
	.get_trip_hyst = wlan_thermal_get_trip_hyst,
	.set_trip_hyst = wlan_thermal_set_trip_hyst,
	.notify = wlan_thermal_notify,
};

static void wlan_thermal_work(struct work_struct *work)
{
	struct wlan_thermal_zone *tzone;
	tzone = container_of(work, struct wlan_thermal_zone, therm_work);
	if ((tzone) && (tzone->tz))
		monitor_thermal_zone(tzone->tz);
}

// WLAN thermal zone driver related initialization...
static int register_wlan_thermal_zone(struct platform_device* pdev,
				      struct thermal_dev*     e_therm_fw,
				      struct thermal_dev*     f_therm_fw)
{
	int ret = 0;
	struct wlan_thermal_zone * tzone;
	struct bcm_thermal_platform_data *pdata;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct bcm_thermal_platform_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	tzone = (struct wlan_thermal_zone*)devm_kzalloc(&pdev->dev, sizeof(struct wlan_thermal_zone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	//copy the default policy
	memcpy(pdata, &wlan_thermal_data_default_policy, sizeof(struct bcm_thermal_platform_data));
	//override the default policy mode so that thermal_zone_device_register creates the thermal zone
	//even though it is disabled as per default policy. We need the themal zone created so that later,
	//user-policy can enable/disable as needed. Current mode will be restored as per default policy
	//at the end of this function.
	pdata->mode  = THERMAL_DEVICE_ENABLED;
	tzone->pdata = pdata;
	tzone->enclosure_therm_fw  = e_therm_fw;
	tzone->fconnector_therm_fw = f_therm_fw;
	tzone->tz = thermal_zone_device_register(e_therm_fw->name,
						 pdata->num_trips,
						 (1 << pdata->num_trips) - 1,
						 tzone,
						 &wlan_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s: Failed to register thermal zone device\n", DRIVER_NAME);
		kfree(tzone);
		return -EINVAL;
	}

	pr_info("%s: Registered WLAN thermal zone...", DRIVER_NAME);
	tzone->tz->trips = pdata->num_trips;
	ret = wlan_thermal_create_sysfs(tzone);
	if( ret )
		return ret;

	INIT_WORK(&tzone->therm_work, wlan_thermal_work);
	platform_set_drvdata(pdev, tzone);
	//restore current mode as per default policy
	pdata->mode = wlan_thermal_data_default_policy.mode;
	return ret;
}

/****************************Individual thermal zone code ends here*****************************/

static int wlan_thermal_probe(struct platform_device *pdev)
{
	int rc;
	struct thermal_dev* enclosure_therm_fw;
	struct thermal_dev* fconnector_therm_fw;

	//record IDME PCBA flag when loading wifi driver
	pcba = (idme_get_board_rev() == 0x0) || (idme_get_usr_flags_value() & USR_FLAGS_PCBA);

	//init thermal_dev for enclosure VS TZ
	enclosure_therm_fw = init_wlan_thermal_dev(pdev);
	if (!enclosure_therm_fw) {
		rc = -ENOMEM;
		goto error;
	}
	rc = enclosure_thermal_dev_register(enclosure_therm_fw);
	if(rc)
		goto error;

	//init thermal_dev for fconnector VS TZ
	fconnector_therm_fw = init_wlan_thermal_dev(pdev);
	if (!fconnector_therm_fw) {
		rc = -ENOMEM;
		goto error;
	}
	rc = fconnector_thermal_dev_register(fconnector_therm_fw);
	if(rc)
		goto error;
	rc = register_wlan_thermal_zone(pdev, enclosure_therm_fw, fconnector_therm_fw);
	if(rc)
		goto error;
error:
	if (rc) {
		pr_err("Error registering %s device as virtual sensor!!\n", fconnector_therm_fw->name);
	}
	else {
		pr_info("Successfully registered %s device as virtual sensor!!\n", fconnector_therm_fw->name);
	}
	return rc;
}

static int wlan_thermal_remove(struct platform_device *pdev)
{
	//nothing to do...
	return 0;
}

static const struct of_device_id wlan_thermal_id_table[] = {
	{ .compatible = "wlan-thermal" },
	{}
};

static struct platform_driver wlan_thermal_driver = {
	.probe = wlan_thermal_probe,
	.remove = wlan_thermal_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = wlan_thermal_id_table,

	},
};

module_platform_driver(wlan_thermal_driver);

MODULE_AUTHOR("Venkat Junnuthulla");
MODULE_DESCRIPTION("WLAN thermal driver");
MODULE_LICENSE("GPL");

