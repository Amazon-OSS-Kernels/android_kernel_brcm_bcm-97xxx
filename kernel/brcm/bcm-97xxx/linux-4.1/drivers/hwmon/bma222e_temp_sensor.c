/*  Amazon Bosch BMA222e Driver
 *
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
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

#include <linux/thermal_framework.h>
#include <linux/platform_data/bcm_thermal.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define BMA222e_METRICS_STR_LEN 128
char *bma222e_metric_prefix = "pcbsensor";
extern const char *product_id2;
#endif
#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#define BMA222e_SENSOR_NAME 		"BMA222e_sensor"
#define BMA222e_CHIP_ID_REG		0x00
#define BMA222e_X_AXIS			0x03
#define BMA222e_Y_AXIS			0x05
#define BMA222e_Z_AXIS			0x07
#define BMA222e_TEMP_REG		0x08
#define BMA222e_MAX_TEMP		127000
#define BMA222e_CHIP_ID_VAL		0xF8
#define BMA223_CHIP_ID_VAL		0xFA
#define BMA222e_TEMP_OFFSET		23
#define BMA222e_X_LSB			0x02
#define BMA222e_X_MSB			0x03
#define BMA222e_Y_LSB			0x04
#define BMA222e_Y_MSB			0x05
#define BMA222e_Z_LSB			0x06
#define BMA222e_Z_MSB			0x07
#define FREE_FALL_LOG_SIZE 		800
#define LOG_REGS 			3
#define BMA222e_2G_SCALE		64  /* to compute mG at 2G resolution divide by this value */
#define BMA222e_ACCEL_IRQ		0x17
#define BMA222e_ACCEL_IRQ_ENABLE	0x08

/* I2C device info */
#define BMA222e_I2C_DEV_BUS		2
#define BMA222e_I2C_DEV_ADDRESS		0x19

#define ORIENTATION_NORMAL		0
#define ORIENTATION_LEFT		1
#define ORIENTATION_RIGHT		2
#define ORIENTATION_DOWN		3
#define ORIENTATION_TOP			4
#define ORIENTATION_ODD			5

/*
 * bma222e_temp_sensor structure
 * @iclient - I2c client pointer
 * @dev - device pointer
 * @sensor_mutex - Mutex for sysfs, irq and PM
 * @therm_fw - thermal device
 */
struct bma222e_temp_sensor {
	struct i2c_client *iclient;
	struct device *dev;
	struct mutex sensor_mutex;
	unsigned long last_update;
	int temp;
	int debug_temp;
};

struct bma222e_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct bcm_thermal_platform_data *pdata;
	struct thermal_dev *enclosure_therm_fw;
	struct thermal_dev *fconnector_therm_fw;
};

struct bma222e_temp_sensor *bma222e_priv;

u8 log_data[FREE_FALL_LOG_SIZE][LOG_REGS];

static inline int bma222e_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static inline int bma222e_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

int bma222e_log_fall(void)
{
	int max_x, max_y, max_z;
	int min_x, min_y, min_z;
	int cur_x, cur_y, cur_z;
	int i;
	int max_g, cur_g;

	if( !bma222e_priv || !bma222e_priv->iclient) {
		printk("bma222e_priv is not initialized\n");
		return 0;
	}

	// collect data.  Keeping loop tight to collect most data
	// loop size of 800 corresponds to about 500msec of data
	for(i=0; i<FREE_FALL_LOG_SIZE; i++) {
		log_data[i][0] = bma222e_read_reg(bma222e_priv->iclient, BMA222e_X_MSB );
		log_data[i][1] = bma222e_read_reg(bma222e_priv->iclient, BMA222e_Y_MSB );
		log_data[i][2] = bma222e_read_reg(bma222e_priv->iclient, BMA222e_Z_MSB );
	}

	// post data capture analysis
	max_x = max_y = max_z = 0;
	min_x = min_y = min_z = 0;
	max_g = 0;
	for(i=0; i<FREE_FALL_LOG_SIZE; i++) {
		cur_x = log_data[i][0];
		cur_y = log_data[i][1];
		cur_z = log_data[i][2];
		if( cur_x & 0x80 )
			cur_x = (-1) * (256 - cur_x);
		if( cur_y & 0x80 )
			cur_y = (-1) * (256 - cur_y);
		if( cur_z & 0x80 )
			cur_z = (-1) * (256 - cur_z);
		cur_g = (cur_x * 1000 / BMA222e_2G_SCALE) +
			(cur_y * 1000 / BMA222e_2G_SCALE) +
			(cur_z * 1000 / BMA222e_2G_SCALE);
		if( cur_g > max_g )
			max_g = cur_g;
		if( cur_x > max_x )
			max_x = cur_x;
		else if( cur_x < min_x )
			min_x = cur_x;
		if( cur_y > max_y )
			max_y = cur_y;
		else if( cur_y < min_y )
			min_y = cur_y;
		if( cur_z > max_z )
			max_z = cur_z;
		else if( cur_z < min_z )
			min_z = cur_z;
	}
	printk("%s:  MAX:  %d, %d, %d\n",__FUNCTION__,max_x, max_y, max_z);
	printk("%s:  MIN:  %d, %d, %d\n",__FUNCTION__,min_x, min_y, min_z);
	printk("%s:  Max mG observed was: %d\n",__FUNCTION__,max_g);
	return max_g;
}
EXPORT_SYMBOL(bma222e_log_fall);

static inline int bma222e_reg_to_mC(u8 val)
{
	val += BMA222e_TEMP_OFFSET;	// val of 0 == 23C
	/*Negative numbers */
	if (val & 0x80) {
		val = ~val + 1;
		return -(val * 1000);
	}
	return val * 1000;
}

/* convert milliCelsius to 8-bit BMA222e register value */
static inline u8 bma222e_mC_to_reg(int val)
{
	return (val / 1000);
}

static int bma222e_read_orientation(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int x_axis, y_axis, z_axis;
	int orientation = ORIENTATION_NORMAL;
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[BMA222e_METRICS_STR_LEN];
#endif

	mutex_lock(&bma222e_priv->sensor_mutex);
	x_axis = bma222e_read_reg(client, BMA222e_X_AXIS) & 0x00ff;
	y_axis = bma222e_read_reg(client, BMA222e_Y_AXIS) & 0x00ff;
	z_axis = bma222e_read_reg(client, BMA222e_Z_AXIS) & 0x00ff;
	mutex_unlock(&bma222e_priv->sensor_mutex);

	if( z_axis > 0x28 && z_axis < 0x58) {
		orientation = ORIENTATION_DOWN;
	} else if( z_axis > 0xb0 && z_axis < 0xd0) {
		orientation = ORIENTATION_NORMAL;
	} else if( x_axis > 0xa8 && x_axis < 0xd8) {
		orientation = ORIENTATION_LEFT;
	} else if( x_axis > 0x28 && x_axis < 0x58) {
		orientation = ORIENTATION_RIGHT;
	} else if( y_axis > 0xb0 && y_axis < 0xd0) {
		orientation = ORIENTATION_TOP;
	} else {
		orientation = ORIENTATION_ODD;
	}
#ifdef CONFIG_AMAZON_METRICS_LOG
	switch(orientation) {
		case ORIENTATION_NORMAL:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_normal=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
		case ORIENTATION_LEFT:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_left=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
		case ORIENTATION_RIGHT:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_right=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
		case ORIENTATION_TOP:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_top=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
		case ORIENTATION_DOWN:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_down=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
		default:
			snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:frank_orientation_odd=1;CT;1:NR", bma222e_metric_prefix, product_id2? product_id2: "def");
			break;
	}
	log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
	return orientation;
}

static int bma222e_read_current_temp(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[BMA222e_METRICS_STR_LEN];
#endif

	mutex_lock(&bma222e_priv->sensor_mutex);
	if (time_after(jiffies, bma222e_priv->last_update + HZ / 3)) {
		int status = bma222e_read_reg(client, BMA222e_TEMP_REG);
		if (status > -1){
			bma222e_priv->temp = bma222e_reg_to_mC(status);
			if(bma222e_priv->temp >= BMA222e_MAX_TEMP){
#ifdef CONFIG_AMAZON_METRICS_LOG
				snprintf(buf, BMA222e_METRICS_STR_LEN,"%s:%s:bma222e_abnormal_temp_read_event=1;CT;1:NR",
					bma222e_metric_prefix, product_id2? product_id2: "def");
				log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			}
		} else {
			bma222e_priv->temp = SENSOR_FAILED_RETURN_TEMP;
#ifdef CONFIG_AMAZON_METRICS_LOG
			snprintf(buf, BMA222e_METRICS_STR_LEN,"%s:%s:bma222e_error_read_event=1;CT;1:NR",
				bma222e_metric_prefix, product_id2? product_id2: "def");
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		}
		bma222e_priv->last_update = jiffies;
	}
	mutex_unlock(&bma222e_priv->sensor_mutex);
	return bma222e_priv->temp;
}

static int bma222e_get_orientation(struct thermal_dev *tdev)
{
	return bma222e_read_orientation(tdev->dev);
}

#define MAX_RETRY 5		/* retry count when PCB temp reading is 127 which could be a faulty read */

/*
 * if bma222e_get_temp returns saved value stgraight 50 times,
 * then return faulty one so system can thermal shutdown.
 */
#define MAX_FAULTY_RETURN_COUNT 50
static int bma222e_get_temp(struct thermal_dev *tdev)
{
	int current_temp;
	static int saved_temp;
	int count = 1;
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[BMA222e_METRICS_STR_LEN];
#endif
	static int fail_count;

	current_temp = bma222e_read_current_temp(tdev->dev);

	if (unlikely(current_temp >= BMA222e_MAX_TEMP)) {
#ifdef CONFIG_AMAZON_METRICS_LOG
		/* Log in metrics */
		snprintf(buf, BMA222e_METRICS_STR_LEN, "%s:%s:bma222e_abnormal_temp=%d;CT;1:NR",
			 bma222e_metric_prefix, product_id2? product_id2: "def", current_temp);
		log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		pr_info("BMA222e reads abnormal temperature %d", current_temp);

		/* retry */
		do {
			current_temp = bma222e_read_current_temp(tdev->dev);
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, BMA222e_METRICS_STR_LEN,
				 "%s:%s:bma222e_temp=%d;CT;1,bma222e_retry=%d;CT;1:NR",
				 bma222e_metric_prefix, product_id2? product_id2: "def", current_temp, count);
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			pr_info("BMA222e reads temperature %d, retrying... %d ", current_temp,
				count);
		} while ((count++ < MAX_RETRY)
			 && (!current_temp || (current_temp >= BMA222e_MAX_TEMP)));

		if (unlikely(current_temp >= BMA222e_MAX_TEMP)) {
			if (++fail_count > MAX_FAULTY_RETURN_COUNT) {
				pr_err("BMA222e temp sensor failed\n");
				return current_temp;
			}
			current_temp = saved_temp;
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, BMA222e_METRICS_STR_LEN,
				 "%s:%s:bma222e_saved_temp=%d;CT;1,bma222e_use_saved_temp=1;CT;1:NR",
				 bma222e_metric_prefix, product_id2? product_id2: "def", saved_temp);
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
			pr_info("WARNING: BMA222e retry failed, return last saved temperature %d",
				saved_temp);
		} else {		/* less than BMA222e_MAX_TEMP read */
			fail_count = 0;
			saved_temp = current_temp;
		}
	}

	saved_temp = current_temp;
	return current_temp;
}

static struct thermal_dev_ops bma222e_temp_sensor_ops = {
	.get_temp = bma222e_get_temp,
};

static ssize_t bma222e_orientation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct bma222e_thermal_zone* tzone = (struct bma222e_thermal_zone*)thermal->devdata;
	int orientation = bma222e_get_orientation( tzone->enclosure_therm_fw );
	ssize_t rc;

	switch(orientation) {
		case ORIENTATION_NORMAL: rc = sprintf(buf, "normal\n"); 	break;
		case ORIENTATION_LEFT:   rc = sprintf(buf, "left\n"); 		break;
		case ORIENTATION_RIGHT:  rc = sprintf(buf, "right\n"); 		break;
		case ORIENTATION_DOWN:   rc = sprintf(buf, "down\n"); 		break;
		case ORIENTATION_TOP:    rc = sprintf(buf, "top\n"); 		break;
		default:                 rc = sprintf(buf, "unknown\n"); 	break;
	}
	return rc;
}
/****************************Individual thermal zone code starts here*****************************/
static const struct bcm_thermal_platform_data bma222e_thermal_data_default_policy = {
       .num_trips = 1,
       .mode = THERMAL_DEVICE_DISABLED,
       .polling_delay = 3000,
       .trips[0] = {.temp = 30000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
                    .cdev[0] = { .type = "pwm-fan", .upper = 1, .lower = 1},},
};

static ssize_t bma222e_temp_sensor_show_params(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct thermal_zone_device* thermal = container_of(dev, struct thermal_zone_device, device);
	struct bma222e_thermal_zone* tzone = (struct bma222e_thermal_zone*)thermal->devdata;
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

static ssize_t bma222e_temp_sensor_set_params(struct device *dev,
						 struct device_attribute *devattr,
						 const char *buf,
						 size_t count)
{
	struct thermal_dev* therm_fw = NULL;
	struct thermal_zone_device* thermal   = container_of(dev, struct thermal_zone_device, device);
	struct bma222e_thermal_zone* tzone = (struct bma222e_thermal_zone*)thermal->devdata;
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

static ssize_t bma222e_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t bma222e_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct bma222e_thermal_zone *tzone = thermal->devdata;
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

static DEVICE_ATTR(orientation, S_IRUGO, bma222e_orientation_show, NULL);
static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, bma222e_polling_show, bma222e_polling_store);
static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, bma222e_temp_sensor_show_params,
                   bma222e_temp_sensor_set_params);

static int bma222e_thermal_create_sysfs(struct bma222e_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s: Failed to create polling attr\n", tzone->tz->type);

	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s: Failed to create params attr\n", tzone->tz->type);

	ret = device_create_file(&tzone->tz->device, &dev_attr_orientation);
	if (ret)
		pr_err("%s: Failed to create orientation attr\n", tzone->tz->type);

	return ret;
}

static int bma222e_match_cdev(struct thermal_cooling_device* cdev,
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

static int bma222e_cdev_bind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (bma222e_match_cdev(cdev, trip, &index))
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

static int bma222e_cdev_unbind(struct thermal_zone_device *thermal,
			    struct thermal_cooling_device *cdev)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (bma222e_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int bma222e_thermal_get_temp(struct thermal_zone_device *thermal,
				       unsigned long *t)
{
	struct bma222e_thermal_zone   *tzone;
	tzone = thermal->devdata;
	*t = bma222e_get_temp(tzone->enclosure_therm_fw);
	return 0;
}

static int bma222e_thermal_get_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode *mode)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&bma222e_priv->sensor_mutex);
	*mode = pdata->mode;
	mutex_unlock(&bma222e_priv->sensor_mutex);
	return 0;
}

static int bma222e_thermal_set_mode(struct thermal_zone_device *thermal,
				 enum thermal_device_mode mode)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&bma222e_priv->sensor_mutex);
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
	mutex_unlock(&bma222e_priv->sensor_mutex);
	return 0;
}

static int bma222e_thermal_get_trip_type(struct thermal_zone_device *thermal,
				      int trip,
				      enum thermal_trip_type *type)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;

	return 0;
}

static int bma222e_thermal_get_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *temp)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}

static int bma222e_thermal_set_trip_temp(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long temp)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
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

static int bma222e_thermal_get_crit_temp(struct thermal_zone_device *thermal,
				      unsigned long *temp)
{
	int i;
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int bma222e_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long *hyst)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;

	return 0;
}
static int bma222e_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
				      int trip,
				      unsigned long hyst)
{
	struct bma222e_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static int bma222e_thermal_notify(struct thermal_zone_device *thermal,
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

static int bma222e_thermal_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	return 0;
}

static struct thermal_zone_device_ops bma222e_tz_dev_ops = {
	.bind = bma222e_cdev_bind,
	.unbind = bma222e_cdev_unbind,
	.get_temp = bma222e_thermal_get_temp,
	.get_mode = bma222e_thermal_get_mode,
	.set_mode = bma222e_thermal_set_mode,
	.set_trips = bma222e_thermal_set_trips,
	.get_trip_type = bma222e_thermal_get_trip_type,
	.get_trip_temp = bma222e_thermal_get_trip_temp,
	.set_trip_temp = bma222e_thermal_set_trip_temp,
	.get_crit_temp = bma222e_thermal_get_crit_temp,
	.get_trip_hyst = bma222e_thermal_get_trip_hyst,
	.set_trip_hyst = bma222e_thermal_set_trip_hyst,
	.notify = bma222e_thermal_notify,
};

static void bma222e_thermal_work(struct work_struct *work)
{
	struct bma222e_thermal_zone *tzone;
	tzone = container_of(work, struct bma222e_thermal_zone, therm_work);
	if ((tzone) && (tzone->tz))
		monitor_thermal_zone(tzone->tz);
}

// bma222e thermal zone driver related initialization...
static int register_bma222e_thermal_zone(struct thermal_dev* e_therm_fw,
                                         struct thermal_dev* f_therm_fw)
{
	int ret = 0;
	struct bma222e_thermal_zone * tzone;
	struct bcm_thermal_platform_data *pdata;

	pdata = devm_kzalloc(bma222e_priv->dev, sizeof(struct bcm_thermal_platform_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	tzone = (struct bma222e_thermal_zone*)devm_kzalloc(bma222e_priv->dev, sizeof(struct bma222e_thermal_zone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	//copy the default policy
	memcpy(pdata, &bma222e_thermal_data_default_policy, sizeof(struct bcm_thermal_platform_data));
	//override the default policy mode so that thermal_zone_device_register creates the thermal zone
	//even though it is disabled as per default policy. We need the themal zone created so that later,
	//user-policy can enable/disable as needed. Current mode will be restored as per default policy
	//at the end of this function.
	pdata->mode   = THERMAL_DEVICE_ENABLED;
	tzone->pdata  = pdata;
	tzone->enclosure_therm_fw  = e_therm_fw;
	tzone->fconnector_therm_fw = f_therm_fw;
	tzone->tz = thermal_zone_device_register(e_therm_fw->name,
						 pdata->num_trips,
						 (1 << pdata->num_trips) - 1,
						 tzone,
						 &bma222e_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s: Failed to register thermal zone device\n", e_therm_fw->name);
		kfree(tzone);
		return -EINVAL;
	}

	pr_info("%s: Registered bma222e thermal zone...", e_therm_fw->name);
	tzone->tz->trips = pdata->num_trips;
	ret = bma222e_thermal_create_sysfs(tzone);
	if(ret)
		return ret;

	INIT_WORK(&tzone->therm_work, bma222e_thermal_work);
	//restore current mode as per default policy
	pdata->mode = bma222e_thermal_data_default_policy.mode;
	return ret;
}
/****************************Individual thermal zone code ends here*****************************/
static struct thermal_dev* init_thermal_fw(struct i2c_client *client)
{
	struct thermal_dev* therm_fw;
	struct device_node *node;
	struct thermal_dev_params *bma222e_tdp;

	therm_fw = kzalloc(sizeof(struct thermal_dev), GFP_KERNEL);
	if (therm_fw) {
		therm_fw->name = BMA222e_SENSOR_NAME;
		therm_fw->dev = bma222e_priv->dev;
		therm_fw->dev_ops = &bma222e_temp_sensor_ops;
#ifdef CONFIG_OF
		node = client->dev.of_node;
		if (!node)
			return NULL;
		bma222e_tdp = kzalloc(sizeof(struct thermal_dev_params), GFP_KERNEL);
		if (!bma222e_tdp)
			return NULL;

		of_property_read_u32(node, "offset", &bma222e_tdp->offset);
		of_property_read_u32(node, "alpha", &bma222e_tdp->alpha);
		of_property_read_u32(node, "weight", &bma222e_tdp->weight);
		therm_fw->tdp = bma222e_tdp;
#else
		therm_fw->tdp = client->dev.platform_data;
#endif
	}
	return therm_fw;
}

static int bma222e_temp_sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct thermal_dev* enclosure_therm_fw  = NULL;
	struct thermal_dev* fconnector_therm_fw = NULL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "adapter doesn't support SMBus word " "transactions\n");

		return -ENODEV;
	}

	bma222e_priv = kzalloc(sizeof(struct bma222e_temp_sensor), GFP_KERNEL);
	if (!bma222e_priv)
		return -ENOMEM;

	mutex_init(&bma222e_priv->sensor_mutex);

	bma222e_priv->iclient = client;
	bma222e_priv->dev = &client->dev;

	kobject_uevent(&client->dev.kobj, KOBJ_ADD);
	i2c_set_clientdata(client, bma222e_priv);

	ret = bma222e_read_reg(client, BMA222e_CHIP_ID_REG);
	if (ret < 0 || ((ret != BMA222e_CHIP_ID_VAL) && (ret != BMA223_CHIP_ID_VAL))) {
		dev_err(&client->dev, "error reading chip_id register\n");
		goto free_err;
	}

	bma222e_priv->last_update = jiffies - HZ;

	//Register to enclosure virtual thermal zone
	enclosure_therm_fw = init_thermal_fw(client);
	if (!enclosure_therm_fw)
		goto tz_create_err;
	ret = enclosure_thermal_dev_register(enclosure_therm_fw);
	if (ret) {
		dev_err(&client->dev, "error registering therml device\n");
		goto tz_create_err;
	}

	//Register to fconnector virtual thermal zone
	fconnector_therm_fw = init_thermal_fw(client);
	if (!fconnector_therm_fw)
		goto tz_create_err;
	ret = fconnector_thermal_dev_register(fconnector_therm_fw);
	if (ret) {
		dev_err(&client->dev, "error registering therml device\n");
		goto tz_create_err;
	}

	//Create it's own thermal zone
	ret = register_bma222e_thermal_zone(enclosure_therm_fw, fconnector_therm_fw);
	if (ret) {
		pr_err("bma222e failed to register as thermal zone %d\n", ret);
		goto tz_create_err;
	}

	// Also enable the accelerometer to interrupt when the device falls.
	bma222e_write_reg( client, BMA222e_ACCEL_IRQ, BMA222e_ACCEL_IRQ_ENABLE );
	dev_info(&client->dev, "initialized\n");
printk("-%s\n",__FUNCTION__);
	return 0;

 tz_create_err:
	kfree(enclosure_therm_fw);
	kfree(fconnector_therm_fw);
 free_err:
	mutex_destroy(&bma222e_priv->sensor_mutex);
	kfree(bma222e_priv);

	return ret;
}

static int bma222e_temp_sensor_remove(struct i2c_client *client)
{
	struct bma222e_temp_sensor *bma222e = i2c_get_clientdata(client);
	kfree(bma222e);
	return 0;
}

static void bma222e_shutdown(struct i2c_client *client)
{
	/* Nothing to do. */
}

#define bma222e_temp_sensor_suspend NULL
#define bma222e_temp_sensor_resume NULL

static const struct i2c_device_id bma222e_id[] = {
	{"bma222e_temp_sensor", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bma222e_id);

#ifdef CONFIG_OF
static const struct of_device_id bma222e_of_match[] = {
	{.compatible = "bosch,bma222e",},
	{},
};
#endif

static struct i2c_driver bma222e_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = bma222e_temp_sensor_probe,
	.remove = bma222e_temp_sensor_remove,
	.driver = {
		.name = "bma222e_temp_sensor",
#ifdef CONFIG_OF
		.of_match_table = bma222e_of_match,
#endif

	},
	.id_table = bma222e_id,
	.shutdown = bma222e_shutdown,
};

#ifndef CONFIG_OF

static struct thermal_dev_params bma222e_platform_data[] = {
	{4700, 3, 400},
};

static struct i2c_board_info bma222ea_board_info __initdata = {
	I2C_BOARD_INFO("bma222e_temp_sensor", BMA222e_I2C_DEV_ADDRESS),
	.platform_data = &bma222e_platform_data[0],
};

#endif

static int __init bma222e_init(void)
{
#ifndef CONFIG_OF
	i2c_register_board_info(BMA222e_I2C_DEV_BUS, &bma222e_board_info, 1);
#endif
	return i2c_add_driver(&bma222e_driver);
}
module_init(bma222e_init);

static void __exit bma222e_exit(void)
{
	i2c_del_driver(&bma222e_driver);
}
module_exit(bma222e_exit);

MODULE_DESCRIPTION("BMA222e Temperature Sensor Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
