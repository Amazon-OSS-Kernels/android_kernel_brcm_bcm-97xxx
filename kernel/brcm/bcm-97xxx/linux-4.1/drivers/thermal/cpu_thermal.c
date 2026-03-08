/*
 * Amazon cpu thermal zone driver based on Broadcom STB AVS TMON thermal sensor driver
 *
 * Copyright (C) 2015 Broadcom Corporation
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 * Author: Siddhartha G Baral <sidbaral@amazon.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#define DRIVER_NAME	"cpu_thermal"

#define pr_fmt(fmt)	DRIVER_NAME ": " fmt

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/thermal_framework.h>
#include <linux/platform_data/bcm_thermal.h>

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#define AVS_TMON_STATUS			0x00
 #define AVS_TMON_STATUS_valid_msk	BIT(11)
 #define AVS_TMON_STATUS_data_msk	GENMASK(10, 1)
 #define AVS_TMON_STATUS_data_shift	1

#define AVS_TMON_EN_OVERTEMP_RESET	0x04
 #define AVS_TMON_EN_OVERTEMP_RESET_msk	BIT(0)

#define AVS_TMON_RESET_THRESH		0x08
 #define AVS_TMON_RESET_THRESH_msk	GENMASK(10, 1)
 #define AVS_TMON_RESET_THRESH_shift	1

#define AVS_TMON_INT_IDLE_TIME		0x10

#define AVS_TMON_EN_TEMP_INT_SRCS	0x14
 #define AVS_TMON_EN_TEMP_INT_SRCS_high	BIT(1)
 #define AVS_TMON_EN_TEMP_INT_SRCS_low	BIT(0)

#define AVS_TMON_INT_THRESH		0x18
 #define AVS_TMON_INT_THRESH_high_msk	GENMASK(26, 17)
 #define AVS_TMON_INT_THRESH_high_shift	17
 #define AVS_TMON_INT_THRESH_low_msk	GENMASK(10, 1)
 #define AVS_TMON_INT_THRESH_low_shift	1

#define AVS_TMON_TEMP_INT_CODE		0x1c
#define AVS_TMON_TP_TEST_ENABLE		0x20

#define CPU_TEMP_CRIT               91000
#define SENSOR_NAME	         "cpu"
#define CPU_THERMAL_PARAM_ALPHA  0
#define CPU_THERMAL_PARAM_OFFSET 0
#define CPU_THERMAL_PARAM_WEIGHT 0

static DEFINE_MUTEX(therm_lock);
static struct thermal_dev *enclosure_therm_fw;
static struct thermal_dev *fconnector_therm_fw;

enum avs_tmon_trip_type {
	TMON_TRIP_TYPE_LOW = 0,
	TMON_TRIP_TYPE_HIGH,
	TMON_TRIP_TYPE_RESET,
	TMON_TRIP_TYPE_MAX,
};

struct avs_tmon_trip {
	/* HW bit to enable the trip */
	u32 enable_offs;
	u32 enable_mask;

	/* HW field to read the trip temperature */
	u32 reg_offs;
	u32 reg_msk;
	int reg_shift;
};

static struct avs_tmon_trip avs_tmon_trips[] = {
	/* Trips when temperature is below threshold */
	[TMON_TRIP_TYPE_LOW] = {
		.enable_offs	= AVS_TMON_EN_TEMP_INT_SRCS,
		.enable_mask	= AVS_TMON_EN_TEMP_INT_SRCS_low,
		.reg_offs	= AVS_TMON_INT_THRESH,
		.reg_msk	= AVS_TMON_INT_THRESH_low_msk,
		.reg_shift	= AVS_TMON_INT_THRESH_low_shift,
	},
	/* Trips when temperature is above threshold */
	[TMON_TRIP_TYPE_HIGH] = {
		.enable_offs	= AVS_TMON_EN_TEMP_INT_SRCS,
		.enable_mask	= AVS_TMON_EN_TEMP_INT_SRCS_high,
		.reg_offs	= AVS_TMON_INT_THRESH,
		.reg_msk	= AVS_TMON_INT_THRESH_high_msk,
		.reg_shift	= AVS_TMON_INT_THRESH_high_shift,
	},
	/* Automatically resets chip when above threshold */
	[TMON_TRIP_TYPE_RESET] = {
		.enable_offs	= AVS_TMON_EN_OVERTEMP_RESET,
		.enable_mask	= AVS_TMON_EN_OVERTEMP_RESET_msk,
		.reg_offs	= AVS_TMON_RESET_THRESH,
		.reg_msk	= AVS_TMON_RESET_THRESH_msk,
		.reg_shift	= AVS_TMON_RESET_THRESH_shift,
	},
};

struct cpu_thermal_zone{
	void __iomem *tmon_base;
	struct device *dev;
	atomic_t intr_temp;
	struct work_struct therm_work;
	struct thermal_zone_device *tz;
	struct bcm_thermal_platform_data* pdata;
};

static struct bcm_thermal_platform_data cpu_thermal_data = {
	.num_trips = 7,
	.mode = THERMAL_DEVICE_ENABLED,
	.polling_delay = 0,
	.trips[0] = {.temp = 0, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 1, .lower = 1},
	},
	.trips[1] = {.temp = 73000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 3, .lower = 2},
        },
	.trips[2] = {.temp = 78000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 4, .lower = 3},
        },
	.trips[3] = {.temp = 82000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 5, .lower = 4},
        },
	.trips[4] = {.temp = 85000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 6, .lower = 5},
        },
	.trips[5] = {.temp = 88000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1500,
		     .cdev[0] = {
			.type = "pwm-fan", .upper = 6, .lower = 6},
	},
	.trips[6] = {.temp = CPU_TEMP_CRIT, .type = THERMAL_TRIP_CRITICAL, .hyst = 1500},
};

/* Convert a HW code to a temperature reading (millidegree celsius) */
static inline int avs_tmon_code_to_temp(u32 code)
{
	return (410040 - (int)((code & 0x3FF) * 487));
}

/*
 * Convert a temperature value (millidegree celsius) to a HW code
 *
 * @temp: temperature to convert
 * @low: if true, round toward the low side
 */
static inline u32 avs_tmon_temp_to_code(int temp, bool low)
{
	if (temp < -88161)
		return 0x3FF;	/* Maximum code value */

	if (temp >= 410040)
		return 0;	/* Minimum code value */

	if (low)
		return (u32)(DIV_ROUND_UP(410040 - temp, 487));
	else
		return (u32)((410040 - temp) / 487);
}

static int cpu_get_cur_temp(struct cpu_thermal_zone *cpu_tzone,
				 unsigned long *temp)
{
	u32 val;
	long t;

	val = __raw_readl(cpu_tzone->tmon_base + AVS_TMON_STATUS);

	if (!(val & AVS_TMON_STATUS_valid_msk)) {
		dev_err(cpu_tzone->dev, "reading not valid\n");

		return -EIO;
	}

	val = (val & AVS_TMON_STATUS_data_msk) >> AVS_TMON_STATUS_data_shift;

	t = avs_tmon_code_to_temp(val);
	if (t < 0)
		*temp = 0;
	else
		*temp = t;

	return 0;
}

static int cpu_get_zone_temp(struct thermal_zone_device *tz,
				 unsigned long *temp)
{
	return cpu_get_cur_temp(tz->devdata, temp);
}

static void avs_tmon_trip_enable(struct cpu_thermal_zone *cpu_tzone,
				 enum avs_tmon_trip_type type, int en)
{
	struct avs_tmon_trip *trip = &avs_tmon_trips[type];
	u32 val = __raw_readl(cpu_tzone->tmon_base + trip->enable_offs);

	pr_debug("%s trip, type %d\n", en ? "enable" : "disable", type);

	if (en)
		val |= trip->enable_mask;
	else
		val &= ~trip->enable_mask;

	__raw_writel(val, cpu_tzone->tmon_base + trip->enable_offs);
}

static int avs_tmon_get_trip_temp(struct cpu_thermal_zone *cpu_tzone,
				  enum avs_tmon_trip_type type)
{
	struct avs_tmon_trip *trip = &avs_tmon_trips[type];
	u32 val = __raw_readl(cpu_tzone->tmon_base + trip->reg_offs);

	val &= trip->reg_msk;
	val >>= trip->reg_shift;

	return avs_tmon_code_to_temp(val);
}

static void avs_tmon_set_trip_temp(struct cpu_thermal_zone *cpu_tzone,
				   enum avs_tmon_trip_type type,
				   int temp)
{
	struct avs_tmon_trip *trip = &avs_tmon_trips[type];
	u32 val, orig;

	pr_debug("set temp %d to %d\n", type, temp);

	/* round toward low temp for the low interrupt */
	val = avs_tmon_temp_to_code(temp, type == TMON_TRIP_TYPE_LOW);

	/* TODO: Check for overflow? */
	val <<= trip->reg_shift;
	val &= trip->reg_msk;

	orig = __raw_readl(cpu_tzone->tmon_base + trip->reg_offs);
	orig &= ~trip->reg_msk;
	orig |= val;
	__raw_writel(orig, cpu_tzone->tmon_base + trip->reg_offs);
}

static int avs_tmon_get_intr_temp(struct cpu_thermal_zone *cpu_tzone)
{
	u32 val;

	val = __raw_readl(cpu_tzone->tmon_base + AVS_TMON_TEMP_INT_CODE);
	return avs_tmon_code_to_temp(val);
}

static irqreturn_t cpu_tmon_irq_thread(int irq, void *data)
{
	struct cpu_thermal_zone *cpu_tzone = data;
	int low, high, intr, ret;

	low = avs_tmon_get_trip_temp(cpu_tzone, TMON_TRIP_TYPE_LOW);
	high = avs_tmon_get_trip_temp(cpu_tzone, TMON_TRIP_TYPE_HIGH);
	intr = avs_tmon_get_intr_temp(cpu_tzone);

	dev_dbg(cpu_tzone->dev, "low/intr/high: %d/%d/%d\n",
			low, intr, high);


	/* Disable high-temp until next threshold shift */
	if (intr >= high)
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_HIGH, 0);
	/* Disable low-temp until next threshold shift */
	if (intr <= low)
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_LOW, 0);

	//update temp
	atomic_set(&(cpu_tzone->intr_temp), intr);

	//schedule work
	ret = schedule_work(&cpu_tzone->therm_work);

	return IRQ_HANDLED;
}

static void cpu_thermal_work(struct work_struct *work)
{
	struct cpu_thermal_zone *tzone;
	tzone = container_of(work, struct cpu_thermal_zone, therm_work);

	if (tzone && tzone->pdata)
		thermal_zone_device_update_temp(tzone->tz, atomic_read(&tzone->intr_temp));
}

static int cpu_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	struct cpu_thermal_zone *cpu_tzone = tz->devdata;

	pr_debug("set trips %lu <--> %lu\n", low, high);

	if (low) {
		if (low > INT_MAX) {
			low = INT_MAX;
		}
		avs_tmon_set_trip_temp(cpu_tzone, TMON_TRIP_TYPE_LOW, (int)low);
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_LOW, 1);
	} else {
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_LOW, 0);
	}

	if (high < ULONG_MAX) {
		if (high > INT_MAX) {
			high = INT_MAX;
		}
		avs_tmon_set_trip_temp(cpu_tzone, TMON_TRIP_TYPE_HIGH, (int)high);
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_HIGH, 1);
	} else {
		avs_tmon_trip_enable(cpu_tzone, TMON_TRIP_TYPE_HIGH, 0);
	}

	return 0;
}

static int cpu_thermal_notify(struct thermal_zone_device *tz,
					 int trip,
					 enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, envp);

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
	if( type == THERMAL_TRIP_CRITICAL )
		life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_SOC);
#endif
	return 0;
}

static int cpu_match_cdev(struct thermal_cooling_device* cdev,
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

static int cpu_cdev_bind(struct thermal_zone_device *thermal,
			 struct thermal_cooling_device *cdev)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (cpu_match_cdev(cdev, trip, &index))
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
	}
	return ret;
}

static int cpu_cdev_unbind(struct thermal_zone_device *thermal,
			   struct thermal_cooling_device *cdev)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (cpu_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int cpu_thermal_get_mode(struct thermal_zone_device *tz,
			       enum thermal_device_mode *mode)
{
	struct cpu_thermal_zone *tzone = tz->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;
	mutex_lock(&therm_lock);
	*mode = pdata->mode;
	mutex_unlock(&therm_lock);

	return 0;
}

static int cpu_thermal_set_mode(struct thermal_zone_device *tz,
			       enum thermal_device_mode mode)
{
	struct cpu_thermal_zone *tzone = tz->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	mutex_lock(&therm_lock);
	pdata->mode = mode;
	if (mode == THERMAL_DEVICE_ENABLED) {
		schedule_work(&tzone->therm_work);
	}
	mutex_unlock(&therm_lock);
	return 0;
}

static int cpu_thermal_get_trip_type(struct thermal_zone_device *thermal,
						int trip,
						enum thermal_trip_type *type)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;

	return 0;
}

static int cpu_thermal_get_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *temp)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;

	return 0;
}

static int cpu_thermal_set_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long temp)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
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

static int cpu_thermal_get_crit_temp(struct thermal_zone_device *thermal,
						unsigned long *temp)
{
	int i;
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}

static int cpu_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *hyst)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;

	return 0;
}
static int cpu_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long hyst)
{
	struct cpu_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static struct thermal_zone_device_ops cpu_tz_dev_ops = {
	.bind          = cpu_cdev_bind,
	.unbind        = cpu_cdev_unbind,
	.get_temp      = cpu_get_zone_temp,
	.get_mode      = cpu_thermal_get_mode,
	.set_mode      = cpu_thermal_set_mode,
	.set_trips     = cpu_set_trips,
	.get_trip_type = cpu_thermal_get_trip_type,
	.get_trip_temp = cpu_thermal_get_trip_temp,
	.set_trip_temp = cpu_thermal_set_trip_temp,
	.get_crit_temp = cpu_thermal_get_crit_temp,
	.get_trip_hyst = cpu_thermal_get_trip_hyst,
	.set_trip_hyst = cpu_thermal_set_trip_hyst,
	.notify = cpu_thermal_notify,
};

static const struct of_device_id cpu_thermal_id_table[] = {
	{ .compatible = "brcm,avs-tmon" },
	{},
};
MODULE_DEVICE_TABLE(of, cpu_thermal_id_table);

static ssize_t cpu_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t cpu_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	// CPU tz driver is interrupt based and polling delay should be always set to 0.
	// Ignore any request for setting polling delay value.
	// We still need to have this "dummy" function available for thermal hal to work properly.
	return count;
}

static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, cpu_polling_show, cpu_polling_store);

static int cpu_create_sysfs(struct thermal_zone_device *tz)
{
	int ret = 0;
	ret = device_create_file(&tz->device, &dev_attr_polling);
	return ret;
}

static int cpu_vs_get_temp(struct thermal_dev* therm_fw)
{
	struct cpu_thermal_zone *cpu_tzone;
	unsigned long temp = 0;
	if (!therm_fw) {
		pr_err("cpu_vs_get_temp: Invalid thermal device\n");
		return 0;
	}
	cpu_tzone = (struct cpu_thermal_zone*)therm_fw->dev;
	if (cpu_get_cur_temp(cpu_tzone, &temp))
		temp = 0;
	return temp;
}

static struct thermal_dev_ops cpu_temp_sensor_ops = {
	.get_temp = cpu_vs_get_temp,
};


static ssize_t cpu_temp_sensor_show_params(struct device *dev,
					   struct device_attribute *devattr, char *buf)
{
	ssize_t len;

	mutex_lock(&therm_lock);
	len = sprintf(buf, "Enclosure offset=%d alpha=%d weight=%d\nFconnector offset=%d alpha=%d weight=%d\n",
			enclosure_therm_fw->tdp->offset,
			enclosure_therm_fw->tdp->alpha,
			enclosure_therm_fw->tdp->weight,
			fconnector_therm_fw->tdp->offset,
			fconnector_therm_fw->tdp->alpha,
			fconnector_therm_fw->tdp->weight);
	mutex_unlock(&therm_lock);
	return len;
}

static ssize_t cpu_temp_sensor_set_params(struct device *dev,
                                          struct device_attribute *devattr,
                                          const char *buf,
                                          size_t count)
{
	struct thermal_dev* therm_fw = NULL;
	char zone[20];
	char param[20];
	int value = 0;

	if (sscanf(buf, "%s %s %d", zone, param, &value) == 3) {
		if( !strcmp( zone, "enclosure"))
			therm_fw = enclosure_therm_fw;
		else if( !strcmp( zone, "fconnector"))
			therm_fw = fconnector_therm_fw;
		else
			return -EINVAL;

		mutex_lock(&therm_lock);
		if (!strcmp(param, "offset"))
			therm_fw->tdp->offset = value;
		else if (!strcmp(param, "alpha"))
			therm_fw->tdp->alpha = value;
		else if (!strcmp(param, "weight"))
			therm_fw->tdp->weight = value;
		else {
			mutex_unlock(&therm_lock);
			return -EINVAL;
		}
		mutex_unlock(&therm_lock);
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, cpu_temp_sensor_show_params,
                   cpu_temp_sensor_set_params);
static int cpu_therm_create_sysfs(struct thermal_zone_device *tz)
{
	int ret = 0;
	ret = device_create_file(&tz->device, &dev_attr_params);
printk("+%s:  ret = %d\n",__FUNCTION__,ret);
	return ret;
}


static struct thermal_dev * init_cpu_thermal_dev(struct platform_device *pdev, struct cpu_thermal_zone *cpu_tzone)
{
	struct thermal_dev* therm_fw;
	therm_fw = devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if (!therm_fw)
		return NULL;
	else {
		therm_fw->tdp  = devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev_params), GFP_KERNEL);
		if (!therm_fw->tdp)
			return NULL;
		else {
			therm_fw->name        = SENSOR_NAME;
			therm_fw->dev         = (struct device *)cpu_tzone;
			therm_fw->dev_ops     = &cpu_temp_sensor_ops;
			therm_fw->tdp->alpha  = CPU_THERMAL_PARAM_ALPHA;
			therm_fw->tdp->offset = CPU_THERMAL_PARAM_OFFSET;
			therm_fw->tdp->weight = CPU_THERMAL_PARAM_WEIGHT;
		}
	}
	return therm_fw;
}

// Register to virtual sensor thermal zone driver...
static int register_cpu_virtual_thermal_zone(struct platform_device *pdev, struct cpu_thermal_zone *cpu_tzone)
{
	int ret = 0;

	enclosure_therm_fw = init_cpu_thermal_dev(pdev, cpu_tzone);
	if (!enclosure_therm_fw) {
		ret = -ENOMEM;
		goto error;
	}
	ret = enclosure_thermal_dev_register(enclosure_therm_fw);

	fconnector_therm_fw = init_cpu_thermal_dev(pdev, cpu_tzone);
	if (!fconnector_therm_fw) {
		ret = -ENOMEM;
		goto error;
	}
	ret = fconnector_thermal_dev_register(fconnector_therm_fw);

error:
	if (ret) {
		pr_err("Error registering cpu thermal device as virtual sensor!!\n");
	}
	else {
		pr_info("Registered CPU as virtual sensor...\n");
	}

	return ret;
}

static int cpu_thermal_zone_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *tz;
	struct bcm_thermal_platform_data *pdata = &cpu_thermal_data;
	struct cpu_thermal_zone *cpu_tzone;
	struct resource *res;
	int irq, ret;

	cpu_tzone = devm_kzalloc(&pdev->dev, sizeof(*cpu_tzone), GFP_KERNEL);
	if (!cpu_tzone)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cpu_tzone->tmon_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cpu_tzone->tmon_base))
		return PTR_ERR(cpu_tzone->tmon_base);

	pdata->mode = THERMAL_DEVICE_ENABLED;
	cpu_tzone->pdata = pdata;
	cpu_tzone->dev = &pdev->dev;
	platform_set_drvdata(pdev, cpu_tzone);

	tz = thermal_zone_device_register("cpu",
					  pdata->num_trips,
					  (1 << pdata->num_trips) - 1,
					  cpu_tzone,
					  &cpu_tz_dev_ops,
					  NULL,
					  0,
					  pdata->polling_delay);

	if (IS_ERR(tz)) {
		dev_err(&pdev->dev, "Failed to register zone device\n");
		return PTR_ERR(tz);
	}

	tz->trips = pdata->num_trips;
	cpu_tzone->tz = tz;
	ret = cpu_create_sysfs(tz);
	if (ret) {
		pr_err("%s: Failed to create polling attr\n", DRIVER_NAME);
		return ret;
	}
	ret = cpu_therm_create_sysfs(tz);
	if (ret) {
		pr_err("%s: Failed to create thermal attr\n", DRIVER_NAME);
		return ret;
	}
	irq = platform_get_irq(pdev, 0);
	INIT_WORK(&cpu_tzone->therm_work, cpu_thermal_work);
	if (irq < 0) {
		dev_err(&pdev->dev, "could not get IRQ\n");
		ret = irq;
		return ret;
	}
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					cpu_tmon_irq_thread, IRQF_ONESHOT,
					DRIVER_NAME, cpu_tzone);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not request IRQ: %d\n", ret);
		return ret;
	}
	dev_info(&pdev->dev, "registered cpu thermal zone driver\n");

	ret = register_cpu_virtual_thermal_zone(pdev, cpu_tzone);

	return ret;
}

static int cpu_thermal_zone_exit(struct platform_device *pdev)
{
	struct cpu_thermal_zone *cpu_tzone = platform_get_drvdata(pdev);
	struct thermal_zone_device *tz = cpu_tzone->tz;

	cancel_work_sync(&cpu_tzone->therm_work);
	if (tz)
		thermal_zone_device_unregister(tz);

	return 0;
}

static struct platform_driver cpu_thermal_driver = {
	.probe = cpu_thermal_zone_probe,
	.remove = cpu_thermal_zone_exit,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cpu_thermal_id_table,
	},
};
module_platform_driver(cpu_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Siddhartha G Baral");
MODULE_DESCRIPTION("Amazon CPU thermal based on Broadcom STB AVS TMON thermal driver");
