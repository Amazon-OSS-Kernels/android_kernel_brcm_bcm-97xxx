/*
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 * Author: Siddhartha G Baral <sidbaral@amazon.com>
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
 * along with this program; if not, you may obtain a copy of the GNU
 * General Public License Version 2 or later at the following locations:
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/reboot.h>
#include <linux/platform_data/bcm_thermal.h>
#include <linux/thermal_framework.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <linux/ata.h>

#include "thermal_core.h"
#include "hdd_thermal.h"

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
#include <linux/sign_of_life.h>
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define HDD_THERMAL_METRICS_STR_LEN 256
char *hdd_thermal_metrics_prefix = "frankthermal";
extern const char *product_id2;
#endif
#define DEV_UPDATE_INTERVAL_SEC 3
#define DEFAULT_MAX_HDD_FAILS 20
static struct scsi_thermal_device hdd_thermal_device = {0};
static DEFINE_MUTEX(hdd_thermal_lock);
static struct thermal_dev *enclosure_therm_fw;
static struct thermal_dev *fconnector_therm_fw;
u64 idme_get_usr_flags_value(void);
unsigned int idme_get_board_rev(void);
#define USR_FLAGS_PCBA (1<<1) /* IDME usr_flags bit 1 */

////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////ATA SMART cmd handler/////////////////////////////////////////////////////////
static void build_identity_device_cmd(unsigned char* scsi_cmd)
{
    static const unsigned char is_cmd_48bit       = 0;
    static const unsigned char need_output_reg    = 0;
    static const unsigned char data_direction     = SCT_CMD_DATA_IN;
    static const unsigned char transaction_dir    = 1;   // 1 means from device; 0 means to device
    static const unsigned char block_unit         = 1;   // 0 means byte unit, 1 means 512 byte block unit
    static const unsigned char transaction_length = 2;   // 0 means no data transferred and 2 means "sector count" field

	// assumes scsi_cmd is a valid 16 byte long array
	if (scsi_cmd)
	{
	    memset(scsi_cmd, 0, ATA_PASSTHROUGH_CMD_LEN);

		scsi_cmd[0]  = ATA_16;
		scsi_cmd[1]  = (data_direction << 1) | is_cmd_48bit;
		scsi_cmd[2]  = (need_output_reg << 5) | (transaction_dir << 3) | (block_unit << 2) | transaction_length;
		scsi_cmd[6]  = 1; //sector count;
		scsi_cmd[14] = ATA_IDENTIFY_DEVICE_CMD;
	}
}

static void build_read_log_e0_cmd(unsigned char* scsi_cmd)
{
    static const unsigned char is_cmd_48bit       = 0;
    static const unsigned char need_output_reg    = 0;
    static const unsigned char data_direction     = SCT_CMD_DATA_IN;
    static const unsigned char transaction_dir    = 1;   // 1 means from device; 0 means to device
    static const unsigned char block_unit         = 1;   // 0 means byte unit, 1 means 512 byte block unit
    static const unsigned char transaction_length = 2;   // 0 means no data transferred and 2 means "sector count" field

	// assumes scsi_cmd is a valid 16 byte long array
	if (scsi_cmd)
	{
	    memset(scsi_cmd, 0, ATA_PASSTHROUGH_CMD_LEN);

		scsi_cmd[0]  = ATA_16;
		scsi_cmd[1]  = (data_direction << 1) | is_cmd_48bit;
		scsi_cmd[2]  = (need_output_reg << 5) | (transaction_dir << 3) | (block_unit << 2) | transaction_length;
		scsi_cmd[4]  = ATA_SMART_READ_LOG_SECTOR; //feature reg;
		scsi_cmd[6]  = 1;                         //sector count;
		scsi_cmd[8]  = 0xe0;                      //lba low;
		scsi_cmd[10] = SMART_CYL_LOW;             //lba mid;
		scsi_cmd[12] = SMART_CYL_HI;              //lba high;
		scsi_cmd[14] = ATA_SMART_CMD;
	}
}

static int alloc_scsi_ata_cmd_buffer(struct platform_device *pdev, struct scsi_ata_cmd_buffer* cmd_buf)
{
	cmd_buf->generic_ata_sect_buf = devm_kzalloc(&pdev->dev, ATA_SECT_SIZE * MAX_ATA_SECTOR_SUPPORTED, GFP_KERNEL);
	if (!cmd_buf->generic_ata_sect_buf)
		return -ENOMEM;
	cmd_buf->generic_sensebuf     = devm_kzalloc(&pdev->dev, SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!cmd_buf->generic_sensebuf) {
		devm_kfree(&pdev->dev, cmd_buf->generic_ata_sect_buf);
		return -ENOMEM;
	}
	cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE] = devm_kzalloc(&pdev->dev, MAX_SCSI_CMD_SIZE * ATA_SCT_CMD_TYPE_MAX, GFP_KERNEL);
	if (!cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE]) {
		devm_kfree(&pdev->dev, cmd_buf->generic_ata_sect_buf);
                devm_kfree(&pdev->dev, cmd_buf->generic_sensebuf);
		return -ENOMEM;
	}
	cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_READ_LOG_E0] = cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE] + MAX_SCSI_CMD_SIZE;
	cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_WRITE_LOG_E0] = cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_READ_LOG_E0] + MAX_SCSI_CMD_SIZE;

	// populate the scsi cmd buffer ahead of time and reuse the same buffer...
	build_identity_device_cmd(cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE]);
	build_read_log_e0_cmd(cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_READ_LOG_E0]);

	return 0;
}

static void free_scsi_ata_cmd_buffer(struct platform_device *pdev, struct scsi_ata_cmd_buffer* cmd_buf)
{
	devm_kfree(&pdev->dev, cmd_buf->generic_ata_sect_buf);
	devm_kfree(&pdev->dev, cmd_buf->generic_sensebuf);
	devm_kfree(&pdev->dev, cmd_buf->scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE]);
}

static bool supports_ata_smart(const struct ata_identify_device_response* res)
{
	if ((res->cmd_set_2 >> 14) == 0x01)
		return ((res->cmd_set_1 & 0x0001) != 0);
	else
		return false;
}

static bool ata_smart_enabled(const struct ata_identify_device_response* res)
{
	if ((res->smart_valid_info >> 14) == 0x01)
		return ((res->smart_en_info & 0x0001) != 0);
	else
		return false;
}

static int get_device_identity(void)
{
	int ret = 0;
	int scsi_status;
	if (hdd_thermal_device.scsi_dev) {
		struct ata_identify_device_response* buffer;
		unsigned char* sense;
		bool ata_sec_locked;
		bool is_sct_capable;

		buffer = (struct ata_identify_device_response*)hdd_thermal_device.cmd_buf.generic_ata_sect_buf;
		sense  = hdd_thermal_device.cmd_buf.generic_sensebuf;

		memset(buffer, 0, sizeof(struct ata_identify_device_response));
		memset(sense, 0, SCSI_SENSE_BUFFERSIZE);

		scsi_status = scsi_execute(hdd_thermal_device.scsi_dev,
						   hdd_thermal_device.cmd_buf.scsi_cmd_array[ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE],
						   DMA_FROM_DEVICE,
						   buffer,
						   ATA_SECT_SIZE,
						   sense, (10*HZ), 5, 0, NULL);

		ret = !scsi_status_is_good(scsi_status);

		if (ret) {
			pr_err("%s: SCSI status is not good!! status = %d",DRIVER_NAME, scsi_status);
			if (driver_byte(scsi_status) == DRIVER_SENSE) {
				pr_err("%s: Need to check SCSI SENSE data!!",DRIVER_NAME);
				// TODO: Do we need to check SENSE data here as we don't have anyway to report that to upper layer??
			}
		}
		else {
			// parse the response and store the values
			if (supports_ata_smart(buffer)) {
				pr_info("%s: SMART is supported in HDD...",SENSOR_NAME);
			}
			else {
				pr_err("%s: SMART is not supported in HDD...",SENSOR_NAME);
			}

			if (ata_smart_enabled(buffer)) {
				pr_info("%s: SMART support is enabled in HDD...",SENSOR_NAME);
			}
			else {
				pr_err("%s: SMART support is not enabled in HDD...",SENSOR_NAME);
			}

			ata_sec_locked = ((buffer->words_88_255[128-88] & 0x0007) == 0x0007);
			if (ata_sec_locked) {
				pr_err("%s: HDD is ATA Security locked!! Can't run SCT cmd!!..",SENSOR_NAME);
			}

			is_sct_capable = !!(buffer->words_88_255[206-88] & 0x01);
			if (!is_sct_capable) {
				pr_err("%s: SCT cmd is not supported!!..",SENSOR_NAME);
			}

			ret = !((supports_ata_smart(buffer) || ata_smart_enabled(buffer)) &&    //SMART capable
				(!ata_sec_locked)					  &&    //ATA sec unlocked
				(is_sct_capable));						//SCT capable
		}
	}

	return ret;
}

static bool old_status_format(struct ata_sct_status_response* sts)
{
	  if (!sts->min_temp && !sts->life_min_temp
	      && !sts->under_limit_count && !sts->over_limit_count){
		  return true;
	  }
	  else {
		  return false;
	  }
}

static int get_sct_status(void)
{
	int ret = 0;
	int scsi_status;
	if (hdd_thermal_device.scsi_dev) {
		struct ata_sct_status_response* buffer;
		unsigned char* sense;

		buffer = (struct ata_sct_status_response*)hdd_thermal_device.cmd_buf.generic_ata_sect_buf;
		sense  = hdd_thermal_device.cmd_buf.generic_sensebuf;

		memset(buffer, 0, sizeof(struct ata_sct_status_response));
		memset(sense, 0, SCSI_SENSE_BUFFERSIZE);

		scsi_status = scsi_execute(hdd_thermal_device.scsi_dev,
						   hdd_thermal_device.cmd_buf.scsi_cmd_array[ATA_SCT_CMD_TYPE_READ_LOG_E0],
						   DMA_FROM_DEVICE,
						   buffer,
						   ATA_SECT_SIZE,
						   sense, (10*HZ), 5, 0, NULL);

		ret = !scsi_status_is_good(scsi_status);

		if (ret) {
			pr_err("%s: SCSI status is not good!! status = %d",DRIVER_NAME, scsi_status);
			if (driver_byte(scsi_status) == DRIVER_SENSE) {
				pr_err("%s: Need to check SCSI SENSE data!!",DRIVER_NAME);
				// TODO: Do we need to check SENSE data here as we don't have anyway to report that to upper layer??
			}
		}
		else {
			// Check format version
			if ((buffer->format_version == 2) || (buffer->format_version == 3)) {
				if (!old_status_format(buffer)) {
					//Check SMART status
					if ((buffer->smart_status) && (buffer->smart_status == 0x2cf4)) {
						ret = -EINVAL;
						pr_err("%s: SMART status is FAILED!!! \n",DRIVER_NAME);
					}
				}
				if (buffer->hda_temp == -128) {
					ret = -EINVAL;
					pr_err("%s: HDD reporting invalid temperature!!! \n",DRIVER_NAME);
				}
				else {
					hdd_thermal_device.temp = buffer->hda_temp;
					if (hdd_thermal_device.temp < 0) {
						hdd_thermal_device.temp = 0;
					}
					// Update the time stamp for last successful temp update
					getnstimeofday(&hdd_thermal_device.update_ts);
				}
			}
			else {
				pr_err("%s: Unknown SCT Status format version %u, should be 2 or 3.\n",DRIVER_NAME, buffer->format_version);
				ret = -EINVAL;
			}
		}
	}

	return ret;
}

// Thermal physical driver related initialization..
static int scsi_thermal_sensor_init(void)
{
	int ret;
	// Make sure the SCSI device is responding and supports SMART cmds
	// before we register this one as thermal zone device
	ret = get_device_identity();
	if (ret) {
		pr_err("%s: ATA SCT cmd mode couldn't be initialized!!!. error = %d", DRIVER_NAME, ret);
		hdd_thermal_device.device_not_supported = 1;
	}
	return ret;
}

static int hdd_get_temp(unsigned long *t)
{
	int ret = 0;
	struct timespec current_ts;
	struct timespec delta_ts;
	static int fail_count=0;
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[HDD_THERMAL_METRICS_STR_LEN];
#endif
	mutex_lock(&hdd_thermal_device.device_mutex);
	if (!hdd_thermal_device.device_ready) {
		*t = HHD_TEMP_DEFAULT;
		if (!hdd_thermal_device.device_not_supported) {
			if (hdd_thermal_device.scsi_dev) {
				ret = scsi_thermal_sensor_init();
				if (!ret) {
					hdd_thermal_device.device_ready = true;
					ret = get_sct_status();
				}
				else
					ret = -ENODEV;
			}
		}
		else {
			ret = -ENODEV;
		}
	}

	if (!ret) {
		getnstimeofday(&current_ts);
		delta_ts = timespec_sub(current_ts, hdd_thermal_device.update_ts);
		if (delta_ts.tv_sec >= DEV_UPDATE_INTERVAL_SEC) {
			ret = get_sct_status();
		}
		if (!ret)
			*t = hdd_thermal_device.temp * 1000; //convert to millidegree...
	}
	mutex_unlock(&hdd_thermal_device.device_mutex);
	if (ret) {
		fail_count++;
#ifdef CONFIG_AMAZON_METRICS_LOG
		/* Log in metrics */
		snprintf(buf, HDD_THERMAL_METRICS_STR_LEN,"%s:%s:hdd_thermal_error_read_count=%d;CT;1:NR",
			hdd_thermal_metrics_prefix, product_id2? product_id2: "def",fail_count);
		log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
	} else {
		if (*t > HDD_MAX_REASONABLE_TEMP) {
			fail_count++;
#ifdef CONFIG_AMAZON_METRICS_LOG
			/* Log in metrics */
			snprintf(buf, HDD_THERMAL_METRICS_STR_LEN,"%s:%s:hdd_thermal_abnormal_temp_read_event;CT;1:NR",
				hdd_thermal_metrics_prefix, product_id2? product_id2: "def");
			log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
		} else {
			fail_count = 0;
		}
	}
	if( fail_count > DEFAULT_MAX_HDD_FAILS && !hdd_thermal_device.pcba)
	{
		pr_err("hdd temp sensor failed\n");
		*t = SENSOR_FAILED_RETURN_TEMP;
	}

	return ret;
}

static int hdd_match_cdev(struct thermal_cooling_device* cdev,
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

static int hdd_cdev_bind(struct thermal_zone_device *thermal,
			 struct thermal_cooling_device *cdev)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];

		if (hdd_match_cdev(cdev, trip, &index))
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

static int hdd_cdev_unbind(struct thermal_zone_device *thermal,
			   struct thermal_cooling_device *cdev)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < thermal->trips; i++) {
		trip = &pdata->trips[i];
		if (hdd_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}

static int hdd_thermal_get_temp(struct thermal_zone_device *thermal,
				unsigned long *t)
{
	int ret;
#ifdef CONFIG_AMAZON_METRICS_LOG
	char buf[HDD_THERMAL_METRICS_STR_LEN];
#endif

	if (!t || !thermal || !thermal->devdata || (thermal->devdata != hdd_thermal_device.hdd_tzone)) {
		pr_err("%s: Invalid thermal zone device %p %p %p",DRIVER_NAME, thermal, thermal->devdata, hdd_thermal_device.hdd_tzone);

		return -EINVAL;
	}

	ret = hdd_get_temp(t);
	if (ret) {
		pr_err("%s: Error getting hdd temp %d",DRIVER_NAME, ret);
#ifdef CONFIG_AMAZON_METRICS_LOG
		/* Log in metrics */
		snprintf(buf, HDD_THERMAL_METRICS_STR_LEN,"%s:%s:hdd_thermal_error_code_%d=1;CT;1:NR", hdd_thermal_metrics_prefix, product_id2? product_id2: "def", ret);
		log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
#endif
	}
	return ret;
}

static int hdd_thermal_get_mode(struct thermal_zone_device *thermal,
					   enum thermal_device_mode *mode)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!pdata)
		return -EINVAL;

	mutex_lock(&hdd_thermal_lock);
	*mode = pdata->mode;
	mutex_unlock(&hdd_thermal_lock);
	return 0;
}

static int hdd_thermal_set_mode(struct thermal_zone_device *thermal,
					   enum thermal_device_mode mode)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&hdd_thermal_lock);
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
	mutex_unlock(&hdd_thermal_lock);
	return 0;
}

static int hdd_thermal_get_trip_type(struct thermal_zone_device *thermal,
						int trip,
						enum thermal_trip_type *type)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;
	return 0;
}

static int hdd_thermal_get_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *temp)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;

	return 0;
}

static int hdd_thermal_set_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long temp)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
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

static int hdd_thermal_get_crit_temp(struct thermal_zone_device *thermal,
						unsigned long *temp)
{
	int i;
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;

			return 0;
		}
	}
	return -EINVAL;
}

static int hdd_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *hyst)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;
	return 0;
}
static int hdd_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long hyst)
{
	struct hdd_thermal_zone *tzone = thermal->devdata;
	struct bcm_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

static int hdd_thermal_notify(struct thermal_zone_device *thermal,
					 int trip,
					 enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&thermal->device.kobj, KOBJ_CHANGE, envp);

#ifdef CONFIG_AMAZON_SIGN_OF_LIFE
	if( type == THERMAL_TRIP_CRITICAL )
		life_cycle_set_thermal_shutdown_reason(THERMAL_SHUTDOWN_REASON_HDD);
#endif
	return 0;
}

static int hdd_thermal_set_trips(struct thermal_zone_device *tz, unsigned long low, unsigned long high)
{
	return 0;
}

static struct thermal_zone_device_ops hdd_tz_dev_ops = {
	.bind = hdd_cdev_bind,
	.unbind = hdd_cdev_unbind,
	.get_temp = hdd_thermal_get_temp,
	.get_mode = hdd_thermal_get_mode,
	.set_mode = hdd_thermal_set_mode,
	.set_trips = hdd_thermal_set_trips,
	.get_trip_type = hdd_thermal_get_trip_type,
	.get_trip_temp = hdd_thermal_get_trip_temp,
	.set_trip_temp = hdd_thermal_set_trip_temp,
	.get_crit_temp = hdd_thermal_get_crit_temp,
	.get_trip_hyst = hdd_thermal_get_trip_hyst,
	.set_trip_hyst = hdd_thermal_set_trip_hyst,
	.notify = hdd_thermal_notify,
};

static ssize_t hdd_polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}

static ssize_t hdd_polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct hdd_thermal_zone *tzone = thermal->devdata;
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

static void hdd_thermal_work(struct work_struct *work)
{
	struct hdd_thermal_zone *tzone;
	tzone = container_of(work, struct hdd_thermal_zone, therm_work);
	if ((tzone) && (tzone->tz))
		monitor_thermal_zone(tzone->tz);
}

static int scsi_device_get_temp(struct thermal_dev* tdev)
{
	int ret;
	unsigned long t = 0;
	ret = hdd_get_temp(&t);
	if (ret) {
		pr_err("%s: Error getting hdd temp %d",DRIVER_NAME, ret);
	}
	return t;
}

static struct thermal_dev_ops hdd_temp_sensor_ops = {
	.get_temp = scsi_device_get_temp,
};

static ssize_t hdd_temp_sensor_show_params(struct device *dev,
					   struct device_attribute *devattr, char *buf)
{
	ssize_t len;
	mutex_lock(&hdd_thermal_device.device_mutex);
	len = sprintf(buf, "Enclosure offset=%d alpha=%d weight=%d\nFconnector offset=%d alpha=%d weight=%d\n",
			enclosure_therm_fw->tdp->offset,
			enclosure_therm_fw->tdp->alpha,
			enclosure_therm_fw->tdp->weight,
			fconnector_therm_fw->tdp->offset,
			fconnector_therm_fw->tdp->alpha,
			fconnector_therm_fw->tdp->weight);
	mutex_unlock(&hdd_thermal_device.device_mutex);
	return len;
}

static ssize_t hdd_temp_sensor_set_params(struct device *dev,
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

		mutex_lock(&hdd_thermal_device.device_mutex);
		if (!strcmp(param, "offset"))
			therm_fw->tdp->offset = value;
		else if (!strcmp(param, "alpha"))
			therm_fw->tdp->alpha = value;
		else if (!strcmp(param, "weight"))
			therm_fw->tdp->weight = value;
		else {
			mutex_unlock(&hdd_thermal_device.device_mutex);
			return -EINVAL;
		}
		mutex_unlock(&hdd_thermal_device.device_mutex);
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR(params, S_IRUGO | S_IWUSR, hdd_temp_sensor_show_params,
                   hdd_temp_sensor_set_params);
static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, hdd_polling_show, hdd_polling_store);

static int hdd_thermal_create_sysfs(struct hdd_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s: Failed to create params attr\n", DRIVER_NAME);

	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s: Failed to create polling attr\n", DRIVER_NAME);
	return ret;
}

// HDD thermal zone driver related initialization...
static int register_hdd_thermal_zone(struct platform_device *pdev)
{
	int ret = 0;
	struct hdd_thermal_zone * tzone;
	struct bcm_thermal_platform_data *pdata = &hdd_thermal_data;


	tzone = (struct hdd_thermal_zone*)devm_kzalloc(&pdev->dev, sizeof(struct hdd_thermal_zone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;

	hdd_thermal_device.hdd_tzone = tzone;
	pdata->mode  = THERMAL_DEVICE_ENABLED;
	tzone->pdata = pdata;
	tzone->tz = thermal_zone_device_register(THERMAL_NAME,
						 pdata->num_trips,
						 (1 << pdata->num_trips) - 1,
						 tzone,
						 &hdd_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s: Failed to register thermal zone device\n", DRIVER_NAME);
		kfree(tzone);
		return -EINVAL;
	}

	pr_info("%s: Registered HDD thermal zone...", DRIVER_NAME);
	tzone->tz->trips = pdata->num_trips;
	ret = hdd_thermal_create_sysfs(tzone);
	if( ret )
		return ret;
	INIT_WORK(&tzone->therm_work, hdd_thermal_work);
	platform_set_drvdata(pdev, tzone);
	return ret;
}

static struct thermal_dev * init_hdd_thermal_dev(struct platform_device *pdev)
{
	struct thermal_dev* therm_fw;

	therm_fw = (struct thermal_dev*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev), GFP_KERNEL);
	if (!therm_fw) {
		return NULL;
	}
	else {
	    therm_fw->tdp  = (struct thermal_dev_params*)devm_kzalloc(&pdev->dev, sizeof(struct thermal_dev_params), GFP_KERNEL);
		if (!therm_fw->tdp) {
			return NULL;
		}
		else {
			therm_fw->name = SENSOR_NAME;
			therm_fw->dev  = NULL;
			therm_fw->dev_ops = &hdd_temp_sensor_ops;
			therm_fw->tdp->alpha  = HDD_THERMAL_PARAM_ALPHA;
			therm_fw->tdp->offset = HDD_THERMAL_PARAM_OFFSET;
			therm_fw->tdp->weight = HDD_THERMAL_PARAM_WEIGHT;
		}
	}
	return therm_fw;
}

// Register to virtual sensor thermal zone driver...
static int register_hdd_virtual_thermal_zone(struct platform_device *pdev)
{
	int ret = 0;

	enclosure_therm_fw = init_hdd_thermal_dev(pdev);
	if (!enclosure_therm_fw) {
		ret = -ENOMEM;
		goto error;
	}
	ret = enclosure_thermal_dev_register(enclosure_therm_fw);

	fconnector_therm_fw = init_hdd_thermal_dev(pdev);
	if (!fconnector_therm_fw) {
		ret = -ENOMEM;
		goto error;
	}
	ret = fconnector_thermal_dev_register(fconnector_therm_fw);

error:
	if (ret) {
		pr_err("Error registering hdd thermal device as virtual sensor!!\n");
	}
	else {
		pr_info("%s: Registered HDD as virtual sensor...", DRIVER_NAME);
	}

	return ret;
}

static int hdd_thermal_probe(struct platform_device *pdev)
{
	int ret;

	mutex_init(&hdd_thermal_device.device_mutex);

	/* board_rev = 0x0 signals unprovisioned IDME, usr_flags bit 1 signals PCBA test stage with no HDD */
	hdd_thermal_device.pcba = (idme_get_board_rev() == 0x0) || (idme_get_usr_flags_value() & USR_FLAGS_PCBA);

	ret = alloc_scsi_ata_cmd_buffer(pdev, &hdd_thermal_device.cmd_buf);
	if (ret) {
		pr_err("%s: Couldn't allocate SCSI ATA cmd buffer!! err = %d",DRIVER_NAME, ret);
		return ret;
	}

	ret = register_hdd_thermal_zone(pdev);
	if (ret)
		return ret;

	ret = register_hdd_virtual_thermal_zone(pdev);
	return ret;
}

static int hdd_thermal_remove(struct platform_device *pdev)
{
	struct hdd_thermal_zone *tzone = platform_get_drvdata(pdev);
	if (tzone) {
		cancel_work_sync(&tzone->therm_work);
		if (tzone->tz)
			thermal_zone_device_unregister(tzone->tz);
		devm_kfree(&pdev->dev, tzone);
	}
	free_scsi_ata_cmd_buffer(pdev, &hdd_thermal_device.cmd_buf);
	mutex_destroy(&hdd_thermal_device.device_mutex);
	return 0;
}

// This is a callback function to get notification for any SCSI device plugin event.
// SG driver will call this as soon as there is a new device plugged in with the right context
// so that HDD thermal driver can store that context and send SCSI cmd to the HDD using that context queue.
int scsi_thermal_dev_register(struct scsi_device* scsid)
{
	if (unlikely(IS_ERR_OR_NULL(scsid))) {
		pr_err("%s: NULL scsi device\n", DRIVER_NAME);
		return -ENODEV;
	}

	// For now, we only expect to work with one internal HDD and don't expect
	// a second one, not even a external USB HDD
	// TODO: Need to enable below mutex after fixing a mutex related hang with this 
	//mutex_lock(&hdd_thermal_device.device_mutex);
	if (!hdd_thermal_device.scsi_dev) {
		hdd_thermal_device.scsi_dev = scsid;
		pr_info("%s: Registered scsi thermal device %p \n", DRIVER_NAME, scsid);
	}
	else {
		pr_err("%s: %p hdd thermal device already registered!!\n", DRIVER_NAME, hdd_thermal_device.scsi_dev);
	}
	//mutex_unlock(&hdd_thermal_device.device_mutex);
	return 0;
}
EXPORT_SYMBOL(scsi_thermal_dev_register);

// This is a callback function to get notification for any SCSI device plug-out event.
// SG driver will call this as soon as there is a scsi device plug-out event so that HDD
// thermal driver stops using the associated SCSI device context.  
int scsi_thermal_dev_unregister(struct scsi_device* scsid)
{
	if (unlikely(IS_ERR_OR_NULL(scsid))) {
		pr_err("%s: NULL scsi therdevice\n", DRIVER_NAME);
		return -ENODEV;
	}

	pr_info("%s: %p removing scsi thermal device\n", DRIVER_NAME, scsid);
	// TODO: Need to enable below mutex after fixing a mutex related hang with this 
	//mutex_lock(&hdd_thermal_device.device_mutex);
	if (hdd_thermal_device.scsi_dev == scsid) {
		hdd_thermal_device.scsi_dev = NULL;
	}
	else {
		pr_err("%s: %p hdd thermal device doesn't match!!!\n", DRIVER_NAME, hdd_thermal_device.scsi_dev);
	}
	//mutex_unlock(&hdd_thermal_device.device_mutex);
	return 0;
}
EXPORT_SYMBOL(scsi_thermal_dev_unregister);

static struct platform_driver hdd_thermal_zone_driver = {
	.probe = hdd_thermal_probe,
	.remove = hdd_thermal_remove,
	.suspend = NULL,
	.resume = NULL,
	.shutdown   = NULL,
	.driver     = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static struct platform_device hdd_thermal_zone_device = {
	.name = DRIVER_NAME,
	.id = -1,
};

static int __init hdd_thermal_init(void)
{
	int ret;
	pr_info("%s: hdd_thermal_init \n", DRIVER_NAME);

	ret = platform_device_register(&hdd_thermal_zone_device);
	if (ret) {
		pr_err("%s: Unable to register hdd device (%d)\n",DRIVER_NAME, ret);
		return ret;
	}

	ret = platform_driver_register(&hdd_thermal_zone_driver);
	if (ret) {
		pr_err("%s: Unable to register hdd thermal driver (%d)\n",DRIVER_NAME, ret);
		return ret;
	}

	return 0;
}

static void __exit hdd_thermal_exit(void)
{
	platform_driver_unregister(&hdd_thermal_zone_driver);
	platform_device_unregister(&hdd_thermal_zone_device);
	pr_info("%s: hdd_thermal_exit \n", DRIVER_NAME);
}

late_initcall(hdd_thermal_init);
module_exit(hdd_thermal_exit);

MODULE_DESCRIPTION("HDD thermal zone driver");
MODULE_AUTHOR("Siddhartha G Baral <sidbaral@amazon.com>");
MODULE_LICENSE("GPL");
