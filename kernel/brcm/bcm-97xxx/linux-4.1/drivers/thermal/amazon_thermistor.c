/*  Amazon Thermistor Driver
 *
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:

 */

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/thermal_framework.h>
#include <linux/platform_data/bcm_thermal.h>
#include "amzn_tmon.h"
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/div64.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#define DBG_PRINT 0
#define WRITE32(addr, val) *(uint32_t *)((volatile uint8_t *)(iobase_addr + addr)) = val
#define READ32(addr) *(uint32_t *)((volatile uint8_t *)(iobase_addr + addr))
#define DEV_UPDATE_INTERVAL_SEC 3
#define MAX_NAME_LENGTH 64

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define THERMISTOR_THERMAL_METRICS_STR_LEN 256
char *thermistor_thermal_metrics_prefix = "thermistorthermal:def";
#endif

static DEFINE_MUTEX(thermistor_thermal_lock);
static struct thermistor_device_data therm_dev_data = {0};
static uint8_t *iobase_addr = NULL;

static const struct bcm_thermal_platform_data thermistor_thermal_data_default_policy = {
       .num_trips = 1,
       .mode = THERMAL_DEVICE_DISABLED,
       .polling_delay = 3000,
       .trips[0] = {.temp = 30000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
                    .cdev[0] = { .type = "pwm-fan", .upper = 1, .lower = 1},},
};

static void amzn_thermistor_enable_tmon(void)
{
	WRITE32( AMZN_TMON_GEN_CTRL, (READ32(AMZN_TMON_GEN_CTRL) | AMZN_TMON_GEN_CTRL_enable_MASK));
	return;
}

static void amzn_thermistor_enable_adc(void)
{
	WRITE32( AMZN_TMON_AFE_CTRL, (READ32(AMZN_TMON_AFE_CTRL) & ~AMZN_TMON_AFE_CTRL_adc_pwrdn_MASK));
	udelay(1000);	// needs delay before we start reading
	return;
}

//write TOP / BOT UPPER bound to 0xfffffffe & TOP / BOT LOWER bound to 0x1 to solve out-of-bound
static void amzn_thermistor_write_bound(void)
{
	WRITE32( AMZN_TMON_TOP_UPPER_BOUNDS, AMZN_TMON_UPPER_LIMIT);
	WRITE32( AMZN_TMON_BOT_UPPER_BOUNDS, AMZN_TMON_UPPER_LIMIT);
	WRITE32( AMZN_TMON_TOP_LOWER_BOUNDS, AMZN_TMON_LOWER_LIMIT);
	WRITE32( AMZN_TMON_BOT_LOWER_BOUNDS, AMZN_TMON_LOWER_LIMIT);
	return;
}

static int calc_temp( uint32_t top, uint32_t mid, uint32_t bottom)
{
	int table_size = sizeof(murata_table)/sizeof(murata_table_t);
	uint32_t rrt = 0;	// relative resistance
	int i;

	// Formula for temperature is
	// Resistor size (47.5Kohm) * relative diff between two halfs
	if( (top - mid) == 0 )	// can't divide by zero
		return 0;
	rrt = (uint32_t)div64_u64( ((uint64_t)THERMISTOR_R_VAL * (mid-bottom)),(top-mid));
	for(i=0; i<table_size; i++)
	{
		// go down table until we find point where rrt > center temp.
		// then check if we're closer to the current setting, or the previous
		// return which ever we're closer to.
		if( rrt >= murata_table[i].rcenter )
		{
			if( i == 0 )
				return murata_table[i].temperature;
			else if( (murata_table[i-1].rcenter - rrt ) > (rrt - murata_table[i].rcenter ) )
				return murata_table[i].temperature;
			else
				return murata_table[i-1].temperature;
		}
	}
	// if we get here, we didn't find a temp in the table, so return highest temp
	return murata_table[i-1].temperature;
}

static void amzn_thermistor_update_device_data(void)
{
	uint32_t status, gen_ctl;
	static uint32_t top, bot, mid0, mid1;

	// if someone's asking for data, let's double check that the enable bit is set
	gen_ctl = READ32(AMZN_TMON_GEN_CTRL);
	if( (gen_ctl & AMZN_TMON_GEN_CTRL_enable_MASK)  == 0 )
	{
		amzn_thermistor_enable_tmon();
	}

	// If data is ready, let's go get it.
	status = READ32( AMZN_TMON_STATUS );
	if( status & AMZN_TMON_STATUS_data_ready_MASK )
	{
		top = READ32(AMZN_TMON_TOP_DATA);
		bot = READ32(AMZN_TMON_BOT_DATA);
		mid0 = READ32(AMZN_TMON_MID0_DATA);
		mid1 = READ32(AMZN_TMON_MID1_DATA);

#if DBG_PRINT
		printk("THERMISTOR:  top:0x%08x, bottom:0x%08x, mid0:0x%08x, mid1:0x%08x, status:0x%08x, ctl: 0x%08x, temp0:%d  temp1:%d\n",
			top & AMZN_TMON_TOP_DATA_top_MASK,
			bot & AMZN_TMON_BOT_DATA_bot_MASK,
			mid0 & AMZN_TMON_MID0_DATA_mid0_MASK,
			mid1 & AMZN_TMON_MID1_DATA_mid1_MASK,
			status, gen_ctl,
			calc_temp( top, mid0, bot),
			calc_temp( top, mid1, bot));
#endif

		// if error in the data, clear the flag and skip the reading
		if( status & AMZN_TMON_STATUS_out_of_bounds_MASK ||
		    status & AMZN_TMON_STATUS_meas_interval_err_MASK )
		{
			printk("%s: out of bounds\n",__FUNCTION__);
			WRITE32( AMZN_TMON_STATUS,
				(AMZN_TMON_STATUS_out_of_bounds_MASK |
				 AMZN_TMON_STATUS_meas_interval_err_MASK |
				 AMZN_TMON_STATUS_data_ready_MASK ));
		}
		else {
			therm_dev_data.top 	= top & AMZN_TMON_TOP_DATA_top_MASK;
			therm_dev_data.bottom 	= bot & AMZN_TMON_BOT_DATA_bot_MASK;
			therm_dev_data.mid1 	= mid1 & AMZN_TMON_MID1_DATA_mid1_MASK;
			therm_dev_data.mid0 	= mid0 & AMZN_TMON_MID0_DATA_mid0_MASK;
			// write to clear data_ready bit
			WRITE32(AMZN_TMON_STATUS, AMZN_TMON_STATUS_data_ready_MASK );
			// Update the time stamp for last successful temp update
			getnstimeofday(&therm_dev_data.update_ts);
		}
	} else {
		pr_err("Error in thermistor temp reading status = %d \n", status);
	}
}

static void amzn_thermistor_hw_init(void)
{
	amzn_thermistor_write_bound();
	amzn_thermistor_enable_adc();
	//use default interval instead of changing
	//amzn_thermistor_set_measurement_interval(HUNDRED_MS_SCAN_INTERVAL);
	amzn_thermistor_enable_tmon();
}

static void amzn_thermistor_update_device_temp(void)
{
	amzn_thermistor_update_device_data();
	therm_dev_data.temp1 = calc_temp(therm_dev_data.top, therm_dev_data.mid1, therm_dev_data.bottom);
	therm_dev_data.temp0 = calc_temp(therm_dev_data.top, therm_dev_data.mid0, therm_dev_data.bottom);
}

static int amzn_thermistor_get_temp(struct thermal_dev *tdev)
{
	struct thermistor_thermal_data *therm_data = (struct thermistor_thermal_data*)tdev->dev;
	struct timespec current_ts;
	struct timespec delta_ts;
	mutex_lock(&thermistor_thermal_lock);
	getnstimeofday(&current_ts);
	delta_ts = timespec_sub(current_ts, therm_dev_data.update_ts);
	if (delta_ts.tv_sec >= DEV_UPDATE_INTERVAL_SEC) {
		amzn_thermistor_update_device_temp();
	}
	mutex_unlock(&thermistor_thermal_lock);
	return (therm_data->which == 1) ? therm_dev_data.temp1 : therm_dev_data.temp0;
}

static struct thermal_dev_ops amzn_thermistor_sensor_ops = {
	.get_temp	= amzn_thermistor_get_temp,
};

static const struct of_device_id amzn_thermistor_thermal_id_table[] = {
	{ .compatible = "amzn_thermistor" },
	{},
};

static struct thermal_dev * init_thermistor_thermal_dev(struct platform_device *pdev)
{
	struct thermistor_thermal_data *therm_data;
	struct thermal_dev *therm_fw;
	struct device_node *node;

	therm_fw = (struct thermal_dev*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if( !therm_fw )
	{
		return NULL;
	} else {
		therm_fw->tdp = (struct thermal_dev_params*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev_params), GFP_KERNEL);
		if (!therm_fw->tdp) {
			return NULL;
		}
		therm_fw->name = (char*)devm_kzalloc(&pdev->dev, MAX_NAME_LENGTH, GFP_KERNEL);
		if (!therm_fw->name) {
			return NULL;
		}

		therm_data = (struct thermistor_thermal_data*)devm_kzalloc(&pdev->dev, sizeof(struct thermistor_thermal_data), GFP_KERNEL);
		if( !therm_data )
			return NULL;
		node = pdev->dev.of_node;
		of_property_read_u32(node, "id", &therm_data->which);
		snprintf(therm_fw->name, MAX_NAME_LENGTH, "thermistor-%d", therm_data->which);

		therm_fw->dev  = (struct device*)therm_data;
		therm_fw->dev_ops = &amzn_thermistor_sensor_ops;

		of_property_read_u32(node, "offset", &therm_fw->tdp->offset);
		of_property_read_u32(node, "alpha", &therm_fw->tdp->alpha);
		of_property_read_u32(node, "weight", &therm_fw->tdp->weight);
	}
	return therm_fw;
}

static ssize_t thermistor_temp_sensor_show_params(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct thermal_zone_device* thermal = container_of(dev, struct thermal_zone_device, device);
	struct thermistor_thermal_zone* tzone = (struct thermistor_thermal_zone*)thermal->devdata;
	ssize_t len;

	len= sprintf(buf, "Enclosure offset=%d alpha=%d weight=%d\nFconnector offset=%d alpha=%d weight=%d\n",
			tzone->enclosure_therm_fw->tdp->offset,
			tzone->enclosure_therm_fw->tdp->alpha,
			tzone->enclosure_therm_fw->tdp->weight,
			tzone->fconnector_therm_fw->tdp->offset,
			tzone->fconnector_therm_fw->tdp->alpha,
			tzone->fconnector_therm_fw->tdp->weight);
	return len;
}

static ssize_t thermistor_temp_sensor_set_params(struct device *dev,
						 struct device_attribute *devattr,
						 const char *buf,
						 size_t count)
{
	struct thermal_dev* therm_fw = NULL;
	struct thermal_zone_device* thermal   = container_of(dev, struct thermal_zone_device, device);
	struct thermistor_thermal_zone* tzone = (struct thermistor_thermal_zone*)thermal->devdata;
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
		else {
			return -EINVAL;
		}
		return count;
	}
	return -EINVAL;
}

static ssize_t thermistor_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t thermistor_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &polling_delay) != 1)
		return -EINVAL;
	if (polling_delay < 0)
		return -EINVAL;

	pdata->polling_delay   = polling_delay;
	thermal->polling_delay = pdata->polling_delay;
	monitor_thermal_zone(thermal);
	return count;
}

static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, thermistor_polling_show, thermistor_polling_store);
static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, thermistor_temp_sensor_show_params,
                   thermistor_temp_sensor_set_params);

static int thermistor_thermal_create_sysfs(struct thermistor_thermal_zone *tzone)
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

static int thermistor_match_cdev(struct thermal_cooling_device* cdev,
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

static int thermistor_cdev_bind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (thermistor_match_cdev(cdev, trip, &index))
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

static int thermistor_cdev_unbind(struct thermal_zone_device *thermal,
			    struct thermal_cooling_device *cdev)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (thermistor_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int thermistor_thermal_get_temp(struct thermal_zone_device *thermal,
				       unsigned long *t)
{
	struct thermistor_thermal_zone   *tzone;

	if (!t || !thermal || !thermal->devdata) {
		pr_err("%s: Invalid thermal zone device %p %p",DRIVER_NAME, thermal, thermal->devdata);
		return -EINVAL;
	}
	tzone = thermal->devdata;
	*t = amzn_thermistor_get_temp(tzone->enclosure_therm_fw);
	return 0;
}

static int thermistor_thermal_get_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode *mode)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&thermistor_thermal_lock);
	*mode = pdata->mode;
	mutex_unlock(&thermistor_thermal_lock);
	return 0;
}

static int thermistor_thermal_set_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode mode)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&thermistor_thermal_lock);
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
	mutex_unlock(&thermistor_thermal_lock);
	return 0;
}

static int thermistor_thermal_get_trip_type(struct thermal_zone_device *thermal,
				      int trip,
				      enum thermal_trip_type *type)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;

	return 0;
}

static int thermistor_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *temp)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}

static int thermistor_thermal_set_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long temp)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
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

static int thermistor_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				      unsigned long *temp)
{
	int i;
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int thermistor_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *hyst)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;

	return 0;
}
static int thermistor_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long hyst)
{
	struct thermistor_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static int thermistor_thermal_notify(struct thermal_zone_device *thermal,
			       int trip,
			       enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&thermal->device.kobj, KOBJ_CHANGE, envp);

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
	if( type == THERMAL_TRIP_CRITICAL )
		life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_PCB);
#endif
	return 0;
}

static int thermistor_thermal_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	return 0;
}

static struct thermal_zone_device_ops thermistor_tz_dev_ops = {
	.bind = thermistor_cdev_bind,
	.unbind = thermistor_cdev_unbind,
	.get_temp = thermistor_thermal_get_temp,
	.get_mode = thermistor_thermal_get_mode,
	.set_mode = thermistor_thermal_set_mode,
	.set_trips = thermistor_thermal_set_trips,
	.get_trip_type = thermistor_thermal_get_trip_type,
	.get_trip_temp = thermistor_thermal_get_trip_temp,
	.set_trip_temp = thermistor_thermal_set_trip_temp,
	.get_crit_temp = thermistor_thermal_get_crit_temp,
	.get_trip_hyst = thermistor_thermal_get_trip_hyst,
	.set_trip_hyst = thermistor_thermal_set_trip_hyst,
	.notify = thermistor_thermal_notify,
};

static void thermistor_thermal_work(struct work_struct *work)
{
	struct thermistor_thermal_zone *tzone;
	tzone = container_of(work, struct thermistor_thermal_zone, therm_work);
	if ((tzone) && (tzone->tz))
		monitor_thermal_zone(tzone->tz);
}

// WLAN thermal zone driver related initialization...
static int register_thermistor_thermal_zone(struct platform_device* pdev,
				      struct thermal_dev*     e_therm_fw,
				      struct thermal_dev*     f_therm_fw)
{
	int ret = 0;
	struct thermistor_thermal_zone * tzone;
	struct bcm_thermal_platform_data *pdata;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct bcm_thermal_platform_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	tzone = (struct thermistor_thermal_zone*)devm_kzalloc(&pdev->dev, sizeof(struct thermistor_thermal_zone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	//copy the default policy
	memcpy(pdata, &thermistor_thermal_data_default_policy, sizeof(struct bcm_thermal_platform_data));

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
						 &thermistor_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s: Failed to register thermal zone device\n", DRIVER_NAME);
		kfree(tzone);
		return -EINVAL;
	}

	pr_info("%s: Registered thermistor thermal zone...", DRIVER_NAME);
	tzone->tz->trips = pdata->num_trips;
	ret = thermistor_thermal_create_sysfs(tzone);
	if( ret )
		return ret;

	INIT_WORK(&tzone->therm_work, thermistor_thermal_work);
	platform_set_drvdata(pdev, tzone);
	//restore current mode as per default policy
	pdata->mode = thermistor_thermal_data_default_policy.mode;
	return ret;
}

/****************************Individual thermal zone code ends here*****************************/

// registers with virtual driver for contribution to policy
static int amzn_therm_reg_virtual(struct platform_device *pdev)
{
	int rc;
	struct thermal_dev* enclosure_therm_fw;
	struct thermal_dev* fconnector_therm_fw;

	//init thermal_dev for enclosure VS TZ
	enclosure_therm_fw = init_thermistor_thermal_dev(pdev);
	if(!enclosure_therm_fw)
		return -ENOMEM;
	rc = enclosure_thermal_dev_register(enclosure_therm_fw);
	if (rc) {
		pr_err("Thermistor failed to register enclosure thermal_dev %d\n", rc);
		return rc;
	}

	//init thermal_dev for fconnector VS TZ
	fconnector_therm_fw = init_thermistor_thermal_dev(pdev);
	if(!fconnector_therm_fw)
		return -ENOMEM;
	rc = fconnector_thermal_dev_register(fconnector_therm_fw);
	if (rc) {
		pr_err("Thermistor failed to register fconnector thermal_dev %d\n", rc);
		return rc;
	}

	rc = register_thermistor_thermal_zone(pdev, enclosure_therm_fw, fconnector_therm_fw);
	if (rc) {
		pr_err("Thermistor failed to register as thermal zone %d\n", rc);
	}
	return rc;
}

static int amzn_thermistor_thermal_probe(struct platform_device *pdev)
{
	int rc = 0;
	if( iobase_addr == NULL ) {
		iobase_addr = (uint8_t *)ioremap(THERMISTOR_BASE_ADDR,THERMISTOR_BASE_ADDR_SIZE);
		amzn_thermistor_hw_init();
		amzn_thermistor_update_device_temp();
	}
	rc = amzn_therm_reg_virtual(pdev);
	if (rc) {
		pr_err("Error registering thermistor sensor as virtual sensor!!\n");
	}
	else {
		pr_info("Successfully registered thermostor sensor as virtual sensor!!\n");
	}
	return rc;
}

static int amzn_thermistor_thermal_remove(struct platform_device *pdev)
{
	//nothing to do.
	return 0;
}

static struct platform_driver amzn_thermistor_thermal_driver = {
	.probe = amzn_thermistor_thermal_probe,
	.remove = amzn_thermistor_thermal_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = amzn_thermistor_thermal_id_table,
	},
};

MODULE_DESCRIPTION("Amazon thermistor driver");
MODULE_DEVICE_TABLE(of, amzn_thermistor_thermal_id_table);
module_platform_driver(amzn_thermistor_thermal_driver);
