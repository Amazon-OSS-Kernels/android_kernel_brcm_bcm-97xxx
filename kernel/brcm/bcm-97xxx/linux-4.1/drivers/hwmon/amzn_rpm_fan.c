/*
 * amzn_rpm_fan.c - Hwmon driver for fans connected to PWM lines.
 *
 * Copyright 2018 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <../thermal/thermal_core.h>
#include <fan_tach.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define FRANK_METRICS_STR_LEN 256
char *frank_fan_metrics_prefix = "frankfan";
#define DEFAULT_FAN_THERMAL_METRICS_DELAY ( 60 * 2 ) /* 120 secs */
extern const char *product_id2;
#endif

#define MAX_PWM 255
#define FAN_CTL_GPIO 171
#define FAN_SPEED_POLL_TIME 2500 /*2500msec*/
#define NUM_MAX_TRIES 5
#define DEFAULT_STEP_TIME 100 /* 200 msec */
#define DEFAULT_STEP_LEVEL 1000
#define DEFAULT_TOLERENCE 48
#define FAN_MONITOR_INTERVAL (1000 * 60 ) /* 60 msec */
#define TIMES_FAN_NOT_AT_TARGET 5
#define FAN_GET_RPM_DIFF 20
#define FAN_GET_RPM_CHECK 5

/* device node to provide fan pwm */
static const struct platform_device *pd = NULL;

static DEFINE_MUTEX(pwm_lock);

struct amzn_rpm_fan_cooling {
	unsigned int *rpm_levels;
	unsigned int *duty_levels;
	unsigned int num_levels;
	bool *valid_flags;
};
struct amzn_rpm_fan_ctx {
	struct mutex lock;
	struct pwm_device *pwm;
	unsigned int rpm_value;
	unsigned int duty_value;
	unsigned int amzn_rpm_fan_state;
	unsigned int amzn_rpm_fan_max_state;
	unsigned int amzn_rpm_fan_pending_state;
	struct amzn_rpm_fan_cooling amzn_rpm_fan_cooling_levels ;
	unsigned int step_time;
	unsigned int step_level;
	unsigned int tolerence;
	unsigned int tach;
#ifdef CONFIG_PM_SLEEP
	unsigned int suspend_state;
#endif
	struct thermal_cooling_device *cdev;
#ifdef CONFIG_AMAZON_METRICS_LOG
	unsigned int fan_thermal_metrics_delay;
	unsigned int fan_state_change_count;
#endif
};

struct work_cont {
	struct delayed_work real_work;
#ifdef CONFIG_AMAZON_METRICS_LOG
	struct delayed_work delay_work;
#endif
	int target_fan_rpm;
	struct amzn_rpm_fan_ctx *ctx;
};

static void fan_work_handler(struct work_struct *w);
#ifdef CONFIG_AMAZON_METRICS_LOG
static void fan_metrics_handler(struct work_struct *w);
static void report_fan_thermal_temp(int amzn_rpm_fan_stat);
static void report_fan_rpm_diff(struct amzn_rpm_fan_ctx *ctx);
#endif
struct work_cont *fan_wq = NULL;
static struct task_struct *fan_monitor_tsk = NULL;
//static struct workqueue_struct *fan_wq = NULL;
//static DECLARE_WORK(fan_work, fan_work_handler);

static int amzn_rpm_fan_disable(struct amzn_rpm_fan_ctx *ctx)
{
	pr_info("Disabling Cooling Device amzn_rpm_fan\n");
	gpio_set_value(FAN_CTL_GPIO, 0x0);
	pwm_disable(ctx->pwm);
	ctx->amzn_rpm_fan_state = 0;
	ctx->duty_value = 0;
	ctx->rpm_value = 0;
	return 0;
}

static int amzn_rpm_fan_enable(struct amzn_rpm_fan_ctx *ctx)
{
	pr_info("Enabling Cooling Device amzn_rpm_fan\n");
	pwm_enable(ctx->pwm);
	gpio_direction_output(FAN_CTL_GPIO, 0x1);
	/* Redundant - but to keep clear */
	gpio_set_value(FAN_CTL_GPIO, 0x1);

	return 0;
}

static int get_amzn_rpm_fan_speed(struct amzn_rpm_fan_ctx *ctx)
{
	int rpm = 0, num_tries = 0;

	do {
		rpm = get_fan_rpm();

		if (rpm < 0 ) {
			msleep(FAN_SPEED_POLL_TIME);
		}
	} while ( rpm < 0 && num_tries++ < NUM_MAX_TRIES);

	return rpm;
}

static int set_amzn_rpm_fan_speed(struct amzn_rpm_fan_ctx *ctx, unsigned long new_duty)
{
	int ret = 0;
	unsigned int duty_step = 0;
	bool amzn_rpm_fan_enabled = false;

	if (new_duty == 0) {
		amzn_rpm_fan_disable(ctx);
		return 0;
	}

	if (ctx->duty_value < new_duty) {
		duty_step = ctx->duty_value + ctx->step_level;
		do {
			ret = pwm_config(ctx->pwm, duty_step, ctx->pwm->period);
			if (ret) {
				return ret;
			}
			if (ctx->rpm_value == 0 && !amzn_rpm_fan_enabled) {
				ret = amzn_rpm_fan_enable(ctx);
				amzn_rpm_fan_enabled = true;
			}
			msleep(ctx->step_time);
			duty_step += ctx->step_level;
		} while (duty_step <= new_duty);
	}
	else if(ctx->duty_value > new_duty) {
		duty_step = ctx->duty_value - ctx->step_level;
		do {
			ret = pwm_config(ctx->pwm, duty_step, ctx->pwm->period);
			if (ret) {
				return ret;
			}
			if (ctx->rpm_value == 0 && !amzn_rpm_fan_enabled) {
				ret = amzn_rpm_fan_enable(ctx);
				amzn_rpm_fan_enabled = true;
			}
			msleep(ctx->step_time);
			duty_step -= ctx->step_level;
		} while (duty_step >= new_duty);
	}
	return ret;
}

void monitor_fan_speed(struct amzn_rpm_fan_ctx *ctx)
{
	static int num_tries = TIMES_FAN_NOT_AT_TARGET;
	static int failure_signaled = false;
	int cur_rpm = 0;
	int upper, lower;

	cur_rpm = get_amzn_rpm_fan_speed(ctx);
	upper = ctx->rpm_value + (ctx->tolerence * 6); // upper limit higher since not as critical
	lower = ctx->rpm_value - (ctx->tolerence + ctx->tolerence / 2);

	if( cur_rpm < 0 || cur_rpm < lower || cur_rpm > upper ) {
		num_tries--;
	} else {
		num_tries = TIMES_FAN_NOT_AT_TARGET;
		failure_signaled = false;
	}

	if( num_tries <= 0 ) {
		num_tries = 0;
		if( !failure_signaled ) {
			printk("ERROR:  Fan actual: %d vs. target: %d\n",cur_rpm,ctx->rpm_value);
#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
			life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_FAN);
#endif
			failure_signaled = true;
		}
	}
}

static int fan_monitor_thread(void *p)
{
	struct amzn_rpm_fan_ctx *ctx = p;
	if( ctx == NULL )
		return -1;

	while (!kthread_should_stop()) {
		monitor_fan_speed(ctx);
		msleep( FAN_MONITOR_INTERVAL );
	}
	return 0;
}

static int update_rpm_levels(struct amzn_rpm_fan_ctx *ctx, unsigned int index)
{
	int ret = 0;
	int cur_rpm = 0;
	int i = 0;
	int sec_rpm = 0;
	bool done = false;
	bool incr=false, decr = false;
	unsigned int duty_cycle = ctx->amzn_rpm_fan_cooling_levels.duty_levels[index];

	do {
		ret = set_amzn_rpm_fan_speed(ctx, duty_cycle);
		if (0 != ret)
			return ret;

		msleep(FAN_SPEED_POLL_TIME);
		cur_rpm = get_amzn_rpm_fan_speed(ctx);

		/* wait a while for fan to settle and check rpm again*/
		/* set rpm if diff is <= 20 and litmit for 5 times*/
		for(i = 0; i < FAN_GET_RPM_CHECK; i++){
			msleep(1000);
			sec_rpm = get_amzn_rpm_fan_speed(ctx);
			if(abs(sec_rpm - cur_rpm) <= FAN_GET_RPM_DIFF){
				cur_rpm = sec_rpm;
				break;
			}
			cur_rpm = sec_rpm;
		}

		if (cur_rpm < 0) {
			return -1;
		}
		else if ((cur_rpm < ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index] - ctx->tolerence) && decr == false) {
			incr = true;
			if (duty_cycle + ctx->step_level >= ctx->amzn_rpm_fan_cooling_levels.duty_levels[ctx->amzn_rpm_fan_max_state]) {
				duty_cycle = ctx->amzn_rpm_fan_cooling_levels.duty_levels[ctx->amzn_rpm_fan_max_state];
				done = true;
			}
			else {
				duty_cycle += ctx->step_level;
			}
		}
		else if ((cur_rpm > ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index] + ctx->tolerence) && incr == false) {
			decr = true;
			if (duty_cycle <= ctx->amzn_rpm_fan_cooling_levels.duty_levels[0] + ctx->step_level) {
				duty_cycle = ctx->amzn_rpm_fan_cooling_levels.duty_levels[0];
				done = true;
			}
			else {
				duty_cycle -= ctx->step_level;
			}
		}
		else {
			/* found closest duty-cycle */
			done = true;
		}
		/* Update the saved RPM value as well */
		ctx->rpm_value = ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index];
		ctx->duty_value = ctx->amzn_rpm_fan_cooling_levels.duty_levels[index];
	} while (!done);

	pr_info("%s: target_fan_rpm = %d, actual_rpm = %d \n",__FUNCTION__,
		ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index], cur_rpm);
	ctx->amzn_rpm_fan_cooling_levels.duty_levels[index] = duty_cycle;
	ctx->amzn_rpm_fan_cooling_levels.valid_flags[index] = true;
	return ret;
}

static int get_rpm_levels_index(struct amzn_rpm_fan_ctx *ctx, unsigned long cur_rpm, unsigned long new_rpm)
{
	int i, index = -1;
	if (cur_rpm > new_rpm ) {
		/* Find closest greater value */
		for (i = 0; i < ctx->amzn_rpm_fan_cooling_levels.num_levels; i++) {
			if (((new_rpm + ctx->tolerence) < ctx->amzn_rpm_fan_cooling_levels.rpm_levels[i]) ) {
				if (i > 0)
					index = i-1;
				else
					index = i;
				break;
			}
		}
	}
	else {
		/* Find closest lesser value */
		for (i = 0; i < ctx->amzn_rpm_fan_cooling_levels.num_levels; i++) {
			if (((new_rpm -  ctx->tolerence) < ctx->amzn_rpm_fan_cooling_levels.rpm_levels[i]) ) {
				index = i;
				break;
			}
		}
	}
	if (index == -1)
		index = ctx->amzn_rpm_fan_cooling_levels.num_levels - 1;
	return index;
}

static int  __set_rpm(struct amzn_rpm_fan_ctx *ctx, unsigned long new_rpm)
{
	unsigned int index;
	bool new_rpm_set = false;
	int ret = 0;
	int i = 0;

	mutex_lock(&ctx->lock);
	if (ctx->rpm_value == new_rpm)
		goto __set_rpm_exit;

	if (0 == new_rpm && ctx->amzn_rpm_fan_state != 0 ) {
		amzn_rpm_fan_disable(ctx);
		goto __set_rpm_exit;
	}

	//duty = DIV_ROUND_UP(pwm * (ctx->pwm->period - 1), MAX_PWM);
	index = get_rpm_levels_index(ctx, ctx->rpm_value, new_rpm);
	while (!new_rpm_set) {
		if (ctx->amzn_rpm_fan_cooling_levels.valid_flags[index] == true) {
			if (0 == ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index] ||
			0 == ctx->amzn_rpm_fan_cooling_levels.duty_levels[index]) {
				amzn_rpm_fan_disable(ctx);
				goto __set_rpm_exit;
			}
			ret = set_amzn_rpm_fan_speed(ctx, ctx->amzn_rpm_fan_cooling_levels.duty_levels[index]);
			if (0 != ret) {
				goto __set_rpm_exit;
			}
			new_rpm_set = true;
			ctx->amzn_rpm_fan_state = index;
			ctx->duty_value = ctx->amzn_rpm_fan_cooling_levels.duty_levels[index];
			ctx->rpm_value =ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index];
		}
		else {
			ret = update_rpm_levels(ctx, index);
			if (0 != ret) {
				goto __set_rpm_exit;
			}
			index = get_rpm_levels_index(ctx, ctx->amzn_rpm_fan_cooling_levels.rpm_levels[index], new_rpm);
		}
	}
	printk("Duty\tRPM\tValid\n");
	for (i = 0; i < ctx->amzn_rpm_fan_cooling_levels.num_levels; i++) {
		printk("%d\t",ctx->amzn_rpm_fan_cooling_levels.duty_levels[i]);
		printk("%d\t",ctx->amzn_rpm_fan_cooling_levels.rpm_levels[i]);
		printk("%d\n",ctx->amzn_rpm_fan_cooling_levels.valid_flags[i]);
	}

__set_rpm_exit:
	mutex_unlock(&ctx->lock);
	return ret;
}

static ssize_t set_rpm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);
	unsigned long rpm;
	int ret;

	if (kstrtoul(buf, 10, &rpm))
		return -EINVAL;

	fan_wq->target_fan_rpm = rpm;
	fan_wq->ctx = ctx;
	cancel_delayed_work(&fan_wq->real_work);	// remove any pending tasks
	ret = queue_delayed_work(system_long_wq, &fan_wq->real_work, 1 * HZ);
	if (ret == 0 ) {
		dev_err(dev, "Cannot set rpm 1!\n");
		return ret;
	}
	return count;
}

static ssize_t show_rpm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ctx->rpm_value);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
                       const char *buf, size_t count)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);
	unsigned long pwm;

	if (kstrtoul(buf, 10, &pwm))
		return -EINVAL;

	if( pwm < 0 || pwm > 100 )
		return -EINVAL;

	if( pwm > 0 ) {
		amzn_rpm_fan_enable(ctx);
		pwm_config(ctx->pwm, (pwm * ctx->pwm->period / 100), ctx->pwm->period);
	} else {
		amzn_rpm_fan_disable(ctx);
	}

	return count;
}

static ssize_t show_pwm(struct device *dev,
                        struct device_attribute *attr, char *buf)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ctx->pwm->duty_cycle);
}

static SENSOR_DEVICE_ATTR(rpm1, S_IRUGO | S_IWUSR, show_rpm, set_rpm, 0);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm, 0);

/* fan pwm for brcmstb-reboot.c */
u32 get_pwm(void)
{
	if (!pd) {
		pr_err("%s: no exposed fan device to get pwm, set pwm as 0\n", __func__);
		return 0;
	}

	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(&(pd->dev));

	return ctx->pwm->duty_cycle;
}

EXPORT_SYMBOL(get_pwm);

static struct attribute *amzn_rpm_fan_attrs[] = {
	&sensor_dev_attr_rpm1.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(amzn_rpm_fan);

/* thermal cooling device callbacks */
static int amzn_rpm_fan_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->amzn_rpm_fan_max_state;

	return 0;
}

static int amzn_rpm_fan_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->amzn_rpm_fan_pending_state;

	return 0;
}

static int
amzn_rpm_fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;
	int ret = 0;

	if (!ctx || (state > ctx->amzn_rpm_fan_max_state))
		return -EINVAL;
	if (state == ctx->amzn_rpm_fan_pending_state)
		return 0;

	ctx->amzn_rpm_fan_pending_state = state;

	fan_wq->target_fan_rpm = ctx->amzn_rpm_fan_cooling_levels.rpm_levels[state];
	fan_wq->ctx = ctx;

	cancel_delayed_work(&fan_wq->real_work);	// remove any pending tasks
	ret = queue_delayed_work(system_long_wq, &fan_wq->real_work, 1 * HZ);
	if (ret == 0 ) {
		dev_err(&cdev->device, "Cannot set rpm! 2\n");
		return ret;
	}

#ifdef CONFIG_AMAZON_METRICS_LOG
	cancel_delayed_work(&fan_wq->delay_work);
	schedule_delayed_work(&fan_wq->delay_work, msecs_to_jiffies(ctx->fan_thermal_metrics_delay * 1000));
	ctx->fan_state_change_count++;
#endif
	return ret;
}

static ssize_t step_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->step_time);
}

static ssize_t step_time_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int step_time = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &step_time) != 1)
		return -EINVAL;
	if (step_time < 0)
		return -EINVAL;

	ctx->step_time = step_time;

	return count;
}

static ssize_t step_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->step_level);
}

static ssize_t step_level_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int step_level = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &step_level) != 1)
		return -EINVAL;
	if (step_level < 0)
		return -EINVAL;

	ctx->step_level = step_level;

	return count;
}

static ssize_t tolerence_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->tolerence);
}

static ssize_t tolerence_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int tolerence = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &tolerence) != 1)
		return -EINVAL;
	if (tolerence < 0)
		return -EINVAL;

	ctx->tolerence = tolerence;

	return count;
}

static ssize_t tach_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->tach);
}

static ssize_t tach_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i, tach = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;
	bool valid_flag = false;
	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &tach) != 1)
		return -EINVAL;
	if (tach < 0)
		return -EINVAL;
	ctx->tach = tach;
	if (ctx->tach == 0)
		valid_flag = true;
	for (i = 0; i < ctx->amzn_rpm_fan_cooling_levels.num_levels; i++) {
		ctx->amzn_rpm_fan_cooling_levels.valid_flags[i] = valid_flag;
	}

	return count;
}

#ifdef CONFIG_AMAZON_METRICS_LOG
static ssize_t fan_thermal_metrics_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->fan_thermal_metrics_delay);
}

static ssize_t fan_thermal_metrics_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int fan_thermal_metrics_delay = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &fan_thermal_metrics_delay) != 1)
		return -EINVAL;
	if (fan_thermal_metrics_delay < 0)
		return -EINVAL;

	ctx->fan_thermal_metrics_delay = fan_thermal_metrics_delay;

	return count;
}

static ssize_t fan_state_change_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return 0;
	return sprintf(buf, "%d\n", ctx->fan_state_change_count);
}

static ssize_t fan_state_change_count_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int fan_state_change_count = 0;
	struct thermal_cooling_device *cdev = container_of(dev, struct thermal_cooling_device, device);
	struct amzn_rpm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &fan_state_change_count) != 1)
		return -EINVAL;
	if (fan_state_change_count != 0)
		return -EINVAL;

	ctx->fan_state_change_count = fan_state_change_count;

	return count;
}
#endif

static DEVICE_ATTR(steptime, S_IRUGO | S_IWUSR, step_time_show, step_time_store);
static DEVICE_ATTR(steplevel, S_IRUGO | S_IWUSR, step_level_show, step_level_store);
static DEVICE_ATTR(tolerence, S_IRUGO | S_IWUSR, tolerence_show, tolerence_store);
static DEVICE_ATTR(tach, S_IRUGO | S_IWUSR, tach_show, tach_store);
#ifdef CONFIG_AMAZON_METRICS_LOG
static DEVICE_ATTR(fan_thermal_metrics_delay, S_IRUGO | S_IWUSR, fan_thermal_metrics_delay_show, fan_thermal_metrics_delay_store);
static DEVICE_ATTR(fan_state_change_count, S_IRUGO | S_IWUSR, fan_state_change_count_show, fan_state_change_count_store);
#endif

static int amzn_rpm_fan_create_sysfs(struct thermal_cooling_device *cdev)
{
	int ret = 0;

	ret = device_create_file(&cdev->device, &dev_attr_steptime);
	if (ret)
		pr_err("%s Failed to create steptime attr\n", __func__);

	ret = device_create_file(&cdev->device, &dev_attr_steplevel);
	if (ret)
		pr_err("%s Failed to create steplevel attr\n", __func__);

	ret = device_create_file(&cdev->device, &dev_attr_tolerence);
	if (ret)
		pr_err("%s Failed to create tolerence attr\n", __func__);

	ret = device_create_file(&cdev->device, &dev_attr_tach);
	if (ret)
		pr_err("%s Failed to create tach attr\n", __func__);

#ifdef CONFIG_AMAZON_METRICS_LOG
	ret = device_create_file(&cdev->device, &dev_attr_fan_thermal_metrics_delay);
	if (ret)
		pr_err("%s Failed to create fan_thermal_metrics_delay attr\n", __func__);

	ret = device_create_file(&cdev->device, &dev_attr_fan_state_change_count);
	if (ret)
		pr_err("%s Failed to create fan_state_change_count attr\n", __func__);
#endif

	return ret;
}

static const struct thermal_cooling_device_ops amzn_rpm_fan_cooling_ops = {
	.get_max_state = amzn_rpm_fan_get_max_state,
	.get_cur_state = amzn_rpm_fan_get_cur_state,
	.set_cur_state = amzn_rpm_fan_set_cur_state,
};

static int amzn_rpm_fan_of_get_cooling_data(struct device *dev,
				       struct amzn_rpm_fan_ctx *ctx)
{
	struct device_node *np = dev->of_node;
	int num, i, ret;

	if (!of_find_property(np, "rpm-levels", NULL))
		return 0;

	if (!of_find_property(np, "num-levels", NULL))
		return 0;

	if (!of_find_property(np, "dutycycle-levels", NULL))
		return 0;

	ret = of_property_read_u32(np, "num-levels", &ctx->amzn_rpm_fan_cooling_levels.num_levels);
	if (ret) {
		dev_err(dev, "Property num-levels cannot be read %d\n", ret);
		return ret;
	}

	num = ctx->amzn_rpm_fan_cooling_levels.num_levels;

	ctx->amzn_rpm_fan_cooling_levels.rpm_levels = devm_kzalloc(dev, num * sizeof(u32),
						   GFP_KERNEL);
	if (!ctx->amzn_rpm_fan_cooling_levels.rpm_levels )
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "rpm-levels",
					 ctx->amzn_rpm_fan_cooling_levels.rpm_levels , num);
	if (ret) {
		dev_err(dev, "Property 'rpm-levels' cannot be read!\n");
		return ret;
	}

	ctx->amzn_rpm_fan_cooling_levels.duty_levels = devm_kzalloc(dev, num * sizeof(u32),
						   GFP_KERNEL);
	if (!ctx->amzn_rpm_fan_cooling_levels.duty_levels )
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "dutycycle-levels",
					 ctx->amzn_rpm_fan_cooling_levels.duty_levels , num);
	if (ret) {
		dev_err(dev, "Property 'dutycycle-levels' cannot be read!\n");
		return ret;
	}

	for (i = 0; i < num; i++) {
		if (ctx->amzn_rpm_fan_cooling_levels.duty_levels[i] > ctx->pwm->period  ) {
			dev_err(dev, "PWM fan state[%d]'s duty-cycle:%d > period:%d\n", i,
				ctx->amzn_rpm_fan_cooling_levels.duty_levels[i], ctx->pwm->period);
			return -EINVAL;
		}
	}
	ctx->amzn_rpm_fan_max_state = num - 1;

	ret = of_property_read_u32(np, "steptime", &ctx->step_time);
	if (ret) {
		dev_err(dev, "Property steptime cannot be read\n");
		ctx->step_time = DEFAULT_STEP_TIME;
	}

	ret = of_property_read_u32(np, "steplevel", &ctx->step_level);
	if (ret) {
		dev_err(dev, "Property steplevel cannot be read\n");
		ctx->step_level = DEFAULT_STEP_LEVEL;
	}

	ret = of_property_read_u32(np, "tolerence", &ctx->tolerence);
	if (ret) {
		dev_warn(dev, "Property tolerence cannot be read\n");
		ctx->tolerence = DEFAULT_TOLERENCE;
	}
	ret = of_property_read_u32(np, "tach", &ctx->tach);
	if (ret) {
		ctx->tach = 1;
	}

#ifdef CONFIG_AMAZON_METRICS_LOG
	ret = of_property_read_u32(np, "fan_thermal_metrics_delay", &ctx->fan_thermal_metrics_delay);
	if (ret) {
		dev_err(dev, "Property fan_thermal_metrics_delay cannot be read\n");
		ctx->fan_thermal_metrics_delay = DEFAULT_FAN_THERMAL_METRICS_DELAY;
	}

	ctx->fan_state_change_count = 0;
#endif

	return 0;
}

static void fan_work_handler(struct work_struct *w)
{
	struct work_cont *c_ptr = container_of(w, struct work_cont, real_work);
	pr_info("+%s: target_fan = %d\n",__FUNCTION__,c_ptr->target_fan_rpm);
	__set_rpm(c_ptr->ctx, c_ptr->target_fan_rpm);
}

#ifdef CONFIG_AMAZON_METRICS_LOG
static void fan_metrics_handler(struct work_struct *w)
{
	struct work_cont *c_ptr = container_of(w, struct work_cont, delay_work);
	report_fan_thermal_temp(c_ptr->ctx->amzn_rpm_fan_state);
	report_fan_rpm_diff(c_ptr->ctx);
}

void report_fan_thermal_temp(int amzn_rpm_fan_stat){
	struct thermal_zone_device *tdev = NULL;
	int fan_state = amzn_rpm_fan_stat;
	int i;
	long temp;

	/* report thermal_zone temp correspond to diff fan zone */
	for(i = 0;;i++){
		tdev = thermal_zone_get_zone_by_id(i);
		if(IS_ERR(tdev))
			break;
		if(tdev->ops->get_temp(tdev, &temp))
			break;
		pr_notice("fan_state_%d_%s_temp=%ld\n",fan_state, tdev->type, temp);
		char buf[FRANK_METRICS_STR_LEN];
		snprintf(buf, FRANK_METRICS_STR_LEN,"%s:%s:fan_state_%d_%s_temp=%ld;CT;1:NR", frank_fan_metrics_prefix, product_id2? product_id2: "def", fan_state, tdev->type, temp);
		log_to_metrics(ANDROID_LOG_INFO, "FanEvent", buf);
	}
}

void report_fan_rpm_diff(struct amzn_rpm_fan_ctx *ctx){
	int rpm_value = get_amzn_rpm_fan_speed(ctx);
	int target_fan_rpm = ctx->rpm_value;
	int fan_rpm_diff = abs(target_fan_rpm - rpm_value);

	/* report diff of actual_fan_rpm and target_fan_rpm if diff > tolerence*/
	if(fan_rpm_diff > ctx->tolerence){
		char buf[FRANK_METRICS_STR_LEN];
		pr_notice("fan_rpm_diff_%s=%d\n", product_id2? product_id2: "def", fan_rpm_diff);
		snprintf(buf, FRANK_METRICS_STR_LEN,"%s:%s:fan_rpm_diff=%d;CT;1:NR", frank_fan_metrics_prefix, product_id2? product_id2: "def", fan_rpm_diff);
		log_to_metrics(ANDROID_LOG_INFO, "FanEvent", buf);
	}
}
#endif

static int amzn_rpm_fan_probe(struct platform_device *pdev)
{
	struct thermal_cooling_device *cdev;
	struct amzn_rpm_fan_ctx *ctx;
	struct device *hwmon;
	int i, ret;
	bool valid_flag = false;

	/* expose device node to get fan pwm */
	pd = pdev;

	if( fan_wq == NULL ) {
		fan_wq = devm_kzalloc(&pdev->dev, sizeof(*fan_wq), GFP_KERNEL);
		if( fan_wq == NULL )
			return -ENOMEM;
		INIT_DELAYED_WORK(&fan_wq->real_work, fan_work_handler);
#ifdef CONFIG_AMAZON_METRICS_LOG
		INIT_DELAYED_WORK(&fan_wq->delay_work, fan_metrics_handler);
#endif
	}

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->pwm = devm_of_pwm_get(&pdev->dev, pdev->dev.of_node, NULL);
	if (IS_ERR(ctx->pwm)) {
		dev_err(&pdev->dev, "Could not get PWM\n");
		return PTR_ERR(ctx->pwm);
	}

	platform_set_drvdata(pdev, ctx);

	hwmon = devm_hwmon_device_register_with_groups(&pdev->dev, "pwmfan",
						       ctx, amzn_rpm_fan_groups);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device\n");
		return PTR_ERR(hwmon);
	}

	ret = amzn_rpm_fan_of_get_cooling_data(&pdev->dev, ctx);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get DT data\n");
		return ret;
	}
	ctx->amzn_rpm_fan_cooling_levels.valid_flags = devm_kzalloc(&pdev->dev, ctx->amzn_rpm_fan_cooling_levels.num_levels  * sizeof(bool),
						   GFP_KERNEL);
	if (!ctx->amzn_rpm_fan_cooling_levels.valid_flags )
		return -ENOMEM;
	if (ctx->tach == 0)
		valid_flag = true;
	for (i = 0; i < ctx->amzn_rpm_fan_cooling_levels.num_levels; i++) {
		ctx->amzn_rpm_fan_cooling_levels.valid_flags[i] = valid_flag;
	}
	amzn_rpm_fan_disable(ctx);
	ctx->amzn_rpm_fan_pending_state = 0;

	if (IS_ENABLED(CONFIG_THERMAL)) {
		cdev = thermal_of_cooling_device_register(pdev->dev.of_node,
							  "pwm-fan", ctx,
							  &amzn_rpm_fan_cooling_ops);
		if (IS_ERR(cdev)) {
			dev_err(&pdev->dev,
				"Failed to register pwm-fan as cooling device");
			return PTR_ERR(cdev);
		}
		ctx->cdev = cdev;
		thermal_cdev_update(cdev);
	}
	amzn_rpm_fan_create_sysfs(ctx->cdev);
	fan_monitor_tsk = kthread_run( &fan_monitor_thread, (void *)ctx, "fan-monitor-thread");

#if 0
	int duty_cycle;
	/* Set duty cycle to maximum allowed */
	duty_cycle = ctx->pwm->period - 1;

	ret = pwm_config(ctx->pwm, duty_cycle, ctx->pwm->period);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure PWM\n");
		return ret;
	}

	/* Enbale PWM output */
	ret = pwm_fan_enable(ctx);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PWM\n");
		return ret;
	}
	ctx->rpm_value = ctx->pwm_fan_cooling_levels.rpm_levels[ctx->pwm_fan_cooling_levels.num_levels - 1];
#endif
	return 0;
}

static int amzn_rpm_fan_remove(struct platform_device *pdev)
{
	struct amzn_rpm_fan_ctx *ctx = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&fan_wq->real_work);	// remove any pending tasks
#ifdef CONFIG_AMAZON_METRICS_LOG
	cancel_delayed_work_sync(&fan_wq->delay_work);
#endif
	thermal_cooling_device_unregister(ctx->cdev);
	if (ctx->rpm_value) {
		amzn_rpm_fan_disable(ctx);
	}
	if( fan_monitor_tsk )
		kthread_stop(fan_monitor_tsk);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int amzn_rpm_fan_suspend(struct device *dev)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);
	ctx->suspend_state = ctx->amzn_rpm_fan_state;
	if (ctx->rpm_value) {
		amzn_rpm_fan_disable(ctx);
	}
	return 0;
}

static int amzn_rpm_fan_resume(struct device *dev)
{
	struct amzn_rpm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	if (ctx->rpm_value == 0)
		return 0;

	//duty = DIV_ROUND_UP(ctx->rpm_value * (ctx->pwm->period - 1), MAX_PWM);
	ret = set_amzn_rpm_fan_speed(ctx, ctx->duty_value);

	ctx->amzn_rpm_fan_state = ctx->suspend_state;

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(amzn_rpm_fan_pm, amzn_rpm_fan_suspend, amzn_rpm_fan_resume);

static const struct of_device_id of_amzn_rpm_fan_match[] = {
	{ .compatible = "amzn_rpm_fan", },
	{},
};

static struct platform_driver amzn_rpm_fan_driver = {
	.probe		= amzn_rpm_fan_probe,
	.remove		= amzn_rpm_fan_remove,
	.driver	= {
		.name		= "amzn_rpm_fan",
		.pm		= &amzn_rpm_fan_pm,
		.of_match_table	= of_amzn_rpm_fan_match,
	},
};

module_platform_driver(amzn_rpm_fan_driver);

MODULE_ALIAS("platform:amzn_rpm_fan");
MODULE_DESCRIPTION("RPM FAN driver");
MODULE_LICENSE("GPL");
