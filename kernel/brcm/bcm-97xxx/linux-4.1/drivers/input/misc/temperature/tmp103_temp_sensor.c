/*
 * tmp103 Temperature sensor driver file
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Steven King <sfking@fdwdc.com>
 * Author: Sabatier, Sebastien" <s-sabatier1@ti.com>
 * Author: Mandrenko, Ievgen" <ievgen.mandrenko@ti.com>
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

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/i2c.h>

#include <linux/input/tmp103_temp_sensor.h>
#include <linux/thermal_framework.h>
#include <linux/platform_data/bcm_thermal.h>
#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define TMP103_METRICS_STR_LEN 128
char *tmp103_metric_prefix = "frankthermal";
extern const char *product_id2;
#endif

#define	TMP103_TEMP_REG			0x00
#define	TMP103_CONF_REG			0x01

#define		TMP103_CONF_M0		0x0001
#define		TMP103_CONF_M1		0x0002
#define		TMP103_CONF_LC		0x0004
#define		TMP103_CONF_FL		0x0008
#define		TMP103_CONF_FH		0x0010
#define		TMP103_CONF_CR0		0x0020
#define		TMP103_CONF_CR1		0x0040
#define		TMP103_CONF_ID		0x0080

#define		TMP103_TLOW_REG		0x02
#define		TMP103_THIGH_REG	0x03

#define		TMP103_SHUTDOWN	0x01
#define		TMP103_MAX_TEMP		127000
#define		TMP103_MAX_REASONABLE_TEMP TMP103_MAX_TEMP

/* I2C device info */
#define TMP103_I2C_DEV_BUS		4
#define TMP103A_I2C_DEV_ADDRESS		0x70
#define TMP103B_I2C_DEV_ADDRESS		0x71
#define TMP103C_I2C_DEV_ADDRESS		0x72
#define TMP103_I2C_BUS_CLK		400
#define DEFAULT_MAX_I2C_FAILS		20

/*
 * omap_temp_sensor structure
 * @iclient - I2c client pointer
 * @dev - device pointer
 * @sensor_mutex - Mutex for sysfs, irq and PM
 * @enclosure_therm_fw - thermal device for enclosure VS
 * @fconnector_therm_fw - thermal device for fconnector VS
 */
struct tmp103_temp_sensor {
	struct i2c_client *iclient;
	struct device *dev;
	struct mutex sensor_mutex;
	u16 config_orig;
	u16 config_current;
	unsigned long last_update;
	int temp;
	int debug_temp;
	int error_limit;
	int saved_temp;
	int fail_count;
};

struct tmp103_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct bcm_thermal_platform_data *pdata;
	struct thermal_dev *enclosure_therm_fw;
	struct thermal_dev *fconnector_therm_fw;
	struct tmp103_temp_sensor *tmp103;
};

static inline int tmp103_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static inline int tmp103_write_reg(struct i2c_client *client, u8 reg, u16 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

static inline int tmp103_reg_to_mC(u8 val)
{
	/*Negative numbers */
	if (val & 0x80) {
		val = ~val + 1;
		return -(val * 1000);
	}
	return val * 1000;
}

/* convert milliCelsius to 8-bit TMP103 register value */
static inline u8 tmp103_mC_to_reg(int val)
{
	return (val / 1000);
}

static const u8 tmp103_reg[] = {
	TMP103_TEMP_REG,
	TMP103_TLOW_REG,
	TMP103_THIGH_REG,
};

static int tmp103_read_current_temp(struct device *dev)
{
	int index = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct tmp103_temp_sensor *tmp103 = i2c_get_clientdata(client);
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[TMP103_METRICS_STR_LEN];
#endif

	tmp103 = i2c_get_clientdata(client);

	mutex_lock(&tmp103->sensor_mutex);
	if (time_after(jiffies, tmp103->last_update + HZ / 3)) {
		int status = tmp103_read_reg(client, tmp103_reg[index]);
		if (status > -1) {
			tmp103->temp = tmp103_reg_to_mC(status);
			if (tmp103->temp >= TMP103_MAX_REASONABLE_TEMP) {
#ifdef CONFIG_AMAZON_METRICS_LOG
				snprintf(buf, TMP103_METRICS_STR_LEN,"%s:%s:tmp103_abnormal_temp_read_event=1;CT;1:NR",
					tmp103_metric_prefix, product_id2? product_id2: "def" );
				log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			}
		} else {
			tmp103->temp = SENSOR_FAILED_RETURN_TEMP;
#ifdef CONFIG_AMAZON_METRICS_LOG
			snprintf(buf, TMP103_METRICS_STR_LEN,"%s:%s:tmp103_error_read_event=1;CT;1:NR",
				tmp103_metric_prefix, product_id2? product_id2: "def" );
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		}
		tmp103->last_update = jiffies;
	}
	mutex_unlock(&tmp103->sensor_mutex);
	return tmp103->temp;
}

#define MAX_RETRY 5		/* retry count when PCB temp reading is 127 which could be a faulty read */

/*
 * if tmp103_get_temp returns saved value stgraight 50 times,
 * then return faulty one so system can thermal shutdown.
 */
#define MAX_FAULTY_RETURN_COUNT 50
static int tmp103_get_temp(struct thermal_dev *tdev)
{
	struct platform_device *pdev = to_platform_device(tdev->dev);
	struct tmp103_temp_sensor *tmp103 = platform_get_drvdata(pdev);
	int current_temp;
	int count = 1;
	struct i2c_client *client = tmp103->iclient;
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[TMP103_METRICS_STR_LEN];
#endif

	current_temp = tmp103_read_current_temp(tdev->dev);

	if (unlikely(current_temp >= TMP103_MAX_TEMP)) {
#ifdef CONFIG_AMAZON_METRICS_LOG
		/* Log in metrics */
		snprintf(buf, TMP103_METRICS_STR_LEN, "%s:%s:tmp103_abnormal_temp=%d;CT;1:NR",
			 tmp103_metric_prefix, product_id2? product_id2: "def", current_temp);
		log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		pr_info("TMP103 reads abnormal temperature %d", current_temp);

		/* switch to shutdown mode, then swtich back to previous mode ( contious conversion mode) */
		tmp103_write_reg(client, TMP103_CONF_REG, 0);	/* shutdown mode: TMP103_CONF_M1=0, TMP103_CONF_M0=0 */
		tmp103_write_reg(client, TMP103_CONF_REG, tmp103->config_current);	/* start conversion */

		/* retry */
		do {
			current_temp = tmp103_read_current_temp(tdev->dev);
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, TMP103_METRICS_STR_LEN,
				 "%s:%s:tmp103_temp=%d;CT;1,tmp103_retry=%d;CT;1:NR",
				 tmp103_metric_prefix, product_id2? product_id2: "def", current_temp, count);
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			pr_info("TMP103 reads temperature %d, retrying... %d ", current_temp,
				count);
		} while ((count++ < MAX_RETRY)
			 && (!current_temp || (current_temp >= TMP103_MAX_TEMP)));

		if (unlikely(current_temp >= TMP103_MAX_TEMP)) {
			if (++tmp103->fail_count > MAX_FAULTY_RETURN_COUNT) {
				pr_err("TMP103 temp sensor failed\n");
				return current_temp;
			}
			current_temp = tmp103->saved_temp;
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, TMP103_METRICS_STR_LEN,
				 "%s:%s:tmp103_saved_temp=%d;CT;1,tmp103_use_saved_temp=1;CT;1:NR",
				 tmp103_metric_prefix, product_id2? product_id2: "def", tmp103->saved_temp);
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			pr_info("WARNING: TMP103 retry failed, return last saved temperature %d",
				tmp103->saved_temp);
		} else {		/* less than TMP103_MAX_TEMP read */
			tmp103->fail_count = 0;
			tmp103->saved_temp = current_temp;
		}
	}

	tmp103->saved_temp = current_temp;
	return current_temp;
}

static struct thermal_dev_ops tmp103_temp_sensor_ops = {
	.get_temp = tmp103_get_temp,
};

/****************************Individual thermal zone code starts here*****************************/
static const struct bcm_thermal_platform_data tmp103_thermal_data_default_policy = {
       .num_trips = 1,
       .mode = THERMAL_DEVICE_DISABLED,
       .polling_delay = 3000,
       .trips[0] = {.temp = 30000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
                    .cdev[0] = { .type = "pwm-fan", .upper = 1, .lower = 1},},
};

static ssize_t tmp103_temp_sensor_show_params(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct thermal_zone_device* thermal = container_of(dev, struct thermal_zone_device, device);
	struct tmp103_thermal_zone* tzone = (struct tmp103_thermal_zone*)thermal->devdata;
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

static ssize_t tmp103_temp_sensor_set_params(struct device *dev,
						 struct device_attribute *devattr,
						 const char *buf,
						 size_t count)
{
	struct thermal_dev* therm_fw = NULL;
	struct thermal_zone_device* thermal   = container_of(dev, struct thermal_zone_device, device);
	struct tmp103_thermal_zone* tzone = (struct tmp103_thermal_zone*)thermal->devdata;
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

static ssize_t tmp103_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t tmp103_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct tmp103_thermal_zone *tzone = thermal->devdata;
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

static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, tmp103_polling_show, tmp103_polling_store);
static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, tmp103_temp_sensor_show_params,
                   tmp103_temp_sensor_set_params);

static int tmp103_thermal_create_sysfs(struct tmp103_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s: Failed to create polling attr\n", tzone->tz->type);

	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s: Failed to create params attr\n", tzone->tz->type);

	return ret;
}

static int tmp103_match_cdev(struct thermal_cooling_device* cdev,
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

static int tmp103_cdev_bind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (tmp103_match_cdev(cdev, trip, &index))
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

static int tmp103_cdev_unbind(struct thermal_zone_device *thermal,
			    struct thermal_cooling_device *cdev)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (tmp103_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int tmp103_thermal_get_temp(struct thermal_zone_device *thermal,
				       unsigned long *t)
{
	struct tmp103_thermal_zone   *tzone;
	tzone = thermal->devdata;
	*t = tmp103_get_temp(tzone->enclosure_therm_fw);
	return 0;
}

static int tmp103_thermal_get_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode *mode)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&tzone->tmp103->sensor_mutex);
	*mode = pdata->mode;
	mutex_unlock(&tzone->tmp103->sensor_mutex);
	return 0;
}

static int tmp103_thermal_set_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode mode)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&tzone->tmp103->sensor_mutex);
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
	mutex_unlock(&tzone->tmp103->sensor_mutex);
	return 0;
}

static int tmp103_thermal_get_trip_type(struct thermal_zone_device *thermal,
				      int trip,
				      enum thermal_trip_type *type)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;

	return 0;
}

static int tmp103_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *temp)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}

static int tmp103_thermal_set_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long temp)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
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

static int tmp103_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				      unsigned long *temp)
{
	int i;
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int tmp103_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *hyst)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;

	return 0;
}
static int tmp103_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long hyst)
{
	struct tmp103_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static int tmp103_thermal_notify(struct thermal_zone_device *thermal,
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

static int tmp103_thermal_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	return 0;
}

static struct thermal_zone_device_ops tmp103_tz_dev_ops = {
	.bind = tmp103_cdev_bind,
	.unbind = tmp103_cdev_unbind,
	.get_temp = tmp103_thermal_get_temp,
	.get_mode = tmp103_thermal_get_mode,
	.set_mode = tmp103_thermal_set_mode,
	.set_trips = tmp103_thermal_set_trips,
	.get_trip_type = tmp103_thermal_get_trip_type,
	.get_trip_temp = tmp103_thermal_get_trip_temp,
	.set_trip_temp = tmp103_thermal_set_trip_temp,
	.get_crit_temp = tmp103_thermal_get_crit_temp,
	.get_trip_hyst = tmp103_thermal_get_trip_hyst,
	.set_trip_hyst = tmp103_thermal_set_trip_hyst,
	.notify = tmp103_thermal_notify,
};

static void tmp103_thermal_work(struct work_struct *work)
{
	struct tmp103_thermal_zone *tzone;
	tzone = container_of(work, struct tmp103_thermal_zone, therm_work);
	if ((tzone) && (tzone->tz))
		monitor_thermal_zone(tzone->tz);
}

// tmp103 thermal zone driver related initialization...
static int register_tmp103_thermal_zone(struct tmp103_temp_sensor* tmp103,
				      struct thermal_dev*        e_therm_fw,
				      struct thermal_dev*        f_therm_fw)
{
	int ret = 0;
	struct tmp103_thermal_zone * tzone;
	struct bcm_thermal_platform_data *pdata;

	pdata = devm_kzalloc(tmp103->dev, sizeof(struct bcm_thermal_platform_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	tzone = (struct tmp103_thermal_zone*)devm_kzalloc(tmp103->dev, sizeof(struct tmp103_thermal_zone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	//copy the default policy
	memcpy(pdata, &tmp103_thermal_data_default_policy, sizeof(struct bcm_thermal_platform_data));
	//override the default policy mode so that thermal_zone_device_register creates the thermal zone
	//even though it is disabled as per default policy. We need the themal zone created so that later,
	//user-policy can enable/disable as needed. Current mode will be restored as per default policy
	//at the end of this function.
	pdata->mode   = THERMAL_DEVICE_ENABLED;
	tzone->tmp103 = tmp103;
	tzone->pdata  = pdata;
	tzone->enclosure_therm_fw  = e_therm_fw;
	tzone->fconnector_therm_fw = f_therm_fw;
	tzone->tz = thermal_zone_device_register(e_therm_fw->name,
						 pdata->num_trips,
						 (1 << pdata->num_trips) - 1,
						 tzone,
						 &tmp103_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s: Failed to register thermal zone device\n", e_therm_fw->name);
		kfree(tzone);
		return -EINVAL;
	}

	pr_info("%s: Registered tmp103 thermal zone...", e_therm_fw->name);
	tzone->tz->trips = pdata->num_trips;
	ret = tmp103_thermal_create_sysfs(tzone);
	if(ret)
		return ret;

	INIT_WORK(&tzone->therm_work, tmp103_thermal_work);
	//restore current mode as per default policy
	pdata->mode = tmp103_thermal_data_default_policy.mode;
	return ret;
}

/****************************Individual thermal zone code ends here*****************************/

static struct thermal_dev* init_thermal_fw(struct i2c_client *client, struct tmp103_temp_sensor *tmp103)
{
	struct thermal_dev* therm_fw;
	struct thermal_dev_params *tmp103_tdp;
	struct device_node *node;
	unsigned int sensor_type;

	therm_fw = (struct thermal_dev*)kzalloc(sizeof(struct thermal_dev), GFP_KERNEL);
	if (therm_fw) {
		therm_fw->dev = tmp103->dev;
		therm_fw->dev_ops = &tmp103_temp_sensor_ops;
#ifdef CONFIG_OF
		node = client->dev.of_node;
		if (!node) {
			kfree(therm_fw);
			return NULL;
		}
		tmp103_tdp = (struct thermal_dev_params*)kzalloc(sizeof(struct thermal_dev_params), GFP_KERNEL);
		if (!tmp103_tdp) {
			kfree(therm_fw);
			return NULL;
		}

		of_property_read_u32(node, "offset", &tmp103_tdp->offset);
		of_property_read_u32(node, "alpha", &tmp103_tdp->alpha);
		of_property_read_u32(node, "weight", &tmp103_tdp->weight);
		of_property_read_u32(node, "error_limit", &tmp103->error_limit);
		of_property_read_u32(node, "reg", &sensor_type);
		if (sensor_type == 0x70) {
			therm_fw->name = TMP103_SENSOR_NAME_0x70;
		} else {
			therm_fw->name = TMP103_SENSOR_NAME_0x71;
		}
		therm_fw->tdp = tmp103_tdp;
#else
		therm_fw->tdp = client->dev.platform_data;
#endif
	}
	return therm_fw;
}

static int tmp103_temp_sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tmp103_temp_sensor *tmp103;
	struct thermal_dev* enclosure_therm_fw  = NULL;
	struct thermal_dev* fconnector_therm_fw = NULL;
	int ret = 0;
	int new;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "adapter doesn't support SMBus word " "transactions\n");

		return -ENODEV;
	}

	tmp103 = (struct tmp103_temp_sensor*)kzalloc(sizeof(struct tmp103_temp_sensor), GFP_KERNEL);
	if (!tmp103)
		return -ENOMEM;

	mutex_init(&tmp103->sensor_mutex);

	tmp103->iclient = client;
	tmp103->dev = &client->dev;

	kobject_uevent(&client->dev.kobj, KOBJ_ADD);
	i2c_set_clientdata(client, tmp103);

	ret = tmp103_read_reg(client, TMP103_CONF_REG);
	if (ret < 0) {
		dev_err(&client->dev, "error reading config register\n");
		goto free_err;
	}
	tmp103->config_orig = ret;

	/* continuous conversions, M1=1, so no need to clear */
	/* Conversion rate settings */
	/* By default, it is set to 4s. Align it to 250ms as used on TI mainline. */
	new = ret & ~0x62;
	new |= 0x42;

	if (ret != new) {
		ret = tmp103_write_reg(client, TMP103_CONF_REG, new);
		if (ret < 0) {
			dev_err(&client->dev, "error writing config register\n");
			goto restore_config_err;
		}
	}

	tmp103->config_current = new;
	tmp103->last_update = jiffies - HZ;
	mutex_init(&tmp103->sensor_mutex);

	//Register to enclosure virtual thermal zone
	enclosure_therm_fw = init_thermal_fw(client, tmp103);
	if (!enclosure_therm_fw)
		goto tz_create_err;
	ret = enclosure_thermal_dev_register(enclosure_therm_fw);
	if (ret) {
		dev_err(&client->dev, "error registering therml device\n");
		goto tz_create_err;
	}

	//Register to fconnector virtual thermal zone
	fconnector_therm_fw = init_thermal_fw(client, tmp103);
	if (!fconnector_therm_fw)
		goto tz_create_err;
	ret = fconnector_thermal_dev_register(fconnector_therm_fw);
	if (ret) {
		dev_err(&client->dev, "error registering therml device\n");
		goto tz_create_err;
	}

	//Create it's own thermal zone
	ret = register_tmp103_thermal_zone(tmp103, enclosure_therm_fw, fconnector_therm_fw);
	if (ret) {
		pr_err("tmp103 failed to register as thermal zone %d\n", ret);
		goto tz_create_err;
	}

	dev_info(&client->dev, "initialized\n");

	return 0;

 tz_create_err:
	kfree(enclosure_therm_fw);
	kfree(fconnector_therm_fw);
 restore_config_err:
	tmp103_write_reg(client, TMP103_CONF_REG, tmp103->config_orig);
 free_err:
	mutex_destroy(&tmp103->sensor_mutex);
	kfree(tmp103);

	return ret;
}

static int tmp103_temp_sensor_remove(struct i2c_client *client)
{
	struct tmp103_temp_sensor *tmp103 = i2c_get_clientdata(client);
	tmp103_write_reg(client, TMP103_CONF_REG, tmp103->config_orig);
	kfree(tmp103);

	return 0;
}

static void tmp103_shutdown(struct i2c_client *client)
{
	/* Nothing to do, since the voltage regulator is on by HW */
}

#if 0				/* TTX-3425: temporarily disable TMP103 suspend/resume functions */
/* #ifdef CONFIG_PM */
static int tmp103_temp_sensor_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int conf = tmp103_read_reg(client, TMP103_CONF_REG);

	if (conf < 0)
		return conf;
	conf |= TMP103_SHUTDOWN;

	return tmp103_write_reg(client, TMP103_CONF_REG, conf);
}

static int tmp103_temp_sensor_resume(struct i2c_client *client)
{
	int conf = tmp103_read_reg(client, TMP103_CONF_REG);

	if (conf < 0)
		return conf;
	conf &= ~TMP103_SHUTDOWN;

	return tmp103_write_reg(client, TMP103_CONF_REG, conf);
}

#else

#define tmp103_temp_sensor_suspend NULL
#define tmp103_temp_sensor_resume NULL

#endif				/* CONFIG_PM */

static const struct i2c_device_id tmp103_id[] = {
	{"tmp103_temp_sensor", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, tmp103_id);

#ifdef CONFIG_OF
static const struct of_device_id tmp103_of_match[] = {
	{.compatible = "ti,tmp103_temp_sensor",},
	{},
};
#endif

static struct i2c_driver tmp103_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = tmp103_temp_sensor_probe,
	.remove = tmp103_temp_sensor_remove,
	.driver = {
		.name = "tmp103_temp_sensor",
#ifdef CONFIG_OF
		.of_match_table = tmp103_of_match,
#endif

	},
	.id_table = tmp103_id,
	.shutdown = tmp103_shutdown,
};

#ifndef CONFIG_OF

static struct thermal_dev_params tmp103_platform_data[3] = {
	{4700, 3, 400},
	{3200, 1000, 0},
	{1300, 1, 600}
};

static struct i2c_board_info tmp103a_board_info __initdata = {
	I2C_BOARD_INFO("tmp103_temp_sensor", TMP103A_I2C_DEV_ADDRESS),
	.platform_data = &tmp103_platform_data[0],
};

static struct i2c_board_info tmp103b_board_info __initdata = {
	I2C_BOARD_INFO("tmp103_temp_sensor", TMP103B_I2C_DEV_ADDRESS),
	.platform_data = &tmp103_platform_data[1],
};

static struct i2c_board_info tmp103c_board_info __initdata = {
	I2C_BOARD_INFO("tmp103_temp_sensor", TMP103C_I2C_DEV_ADDRESS),
	.platform_data = &tmp103_platform_data[2],
};
#endif

static int __init tmp103_init(void)
{
#ifndef CONFIG_OF
	i2c_register_board_info(TMP103_I2C_DEV_BUS, &tmp103a_board_info, 1);
	i2c_register_board_info(TMP103_I2C_DEV_BUS, &tmp103b_board_info, 1);
	i2c_register_board_info(TMP103_I2C_DEV_BUS, &tmp103c_board_info, 1);
#endif
	return i2c_add_driver(&tmp103_driver);
}
module_init(tmp103_init);

static void __exit tmp103_exit(void)
{
	i2c_del_driver(&tmp103_driver);
}
module_exit(tmp103_exit);

MODULE_DESCRIPTION("OMAP44XX tmp103 Temperature Sensor Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Texas Instruments Inc");
