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
#ifndef _HDD_THERMAL_
#define _HDD_THERMAL_

////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////ATA SMART cmd handler/////////////////////////////////////////////////////////
#define ATTR_PACKED __attribute__((packed))

#define ATA_PASSTHROUGH_CMD_LEN             16

#define ATA_IDENTIFY_DEVICE_CMD             0xec
#define ATA_SMART_CMD                       0xb0

#define SCT_CMD_DATA_NONE                   0x03
#define SCT_CMD_DATA_IN                     0x04
#define SCT_CMD_DATA_OUT                    0x05

#define SMART_CYL_LOW  0x4F
#define SMART_CYL_HI   0xC2

//FR values
#define ATA_SMART_READ_LOG_SECTOR           0xd5

// we reuse the same ATA SECTOR buffer for all ATA cmds as scsi cmds are executed serially.
// Bump up the below number if we need to add support for new ATA/SCT cmds which will need
// more ATA SECTOR than we currently support i.e MAX_ATA_SECTOR_SUPPORTED. With this approach
// we don't have to malloc/free buffer for each ata/sct cmd.
#define MAX_ATA_SECTOR_SUPPORTED 1
#define MAX_SCSI_CMD_SIZE        16

// we currently support only below ATA/SCT cmds
enum ATA_SCT_CMD_TYPE {
	ATA_SCT_CMD_TYPE_IDENTIFY_DEVICE = 0,
	ATA_SCT_CMD_TYPE_READ_LOG_E0     = 1,
	ATA_SCT_CMD_TYPE_WRITE_LOG_E0    = 2,
	ATA_SCT_CMD_TYPE_MAX             = 3,
};

struct scsi_ata_cmd_buffer {
	unsigned char* generic_ata_sect_buf;
	unsigned char* generic_sensebuf;
	unsigned char* scsi_cmd_array[ATA_SCT_CMD_TYPE_MAX];
};

#pragma pack(1)
struct ata_identify_device_response {
  unsigned short words_0_9[10];
  unsigned char  sr_no[20];
  unsigned short words_20_22[3];
  unsigned char  fw_revision[8];
  unsigned char  model_name[40];
  unsigned short words_47_79[33];
  unsigned short major_rev;
  unsigned short minor_rev;
  unsigned short cmd_set_1;
  unsigned short cmd_set_2;
  unsigned short cmd_set_ext;
  unsigned short smart_en_info;
  unsigned short words_86;
  unsigned short smart_valid_info;
  unsigned short words_88_255[168];
} ATTR_PACKED;
#pragma pack()

#pragma pack(1)
struct ata_sct_status_response
{
  unsigned short format_version;          // 0-1: Status response format version number (2, 3)
  unsigned short sct_version;             // 2-3: Vendor specific version number
  unsigned short sct_spec;                // 4-5: SCT level supported (1)
  unsigned int   status_flags;            // 6-9: Status flags (Bit 0: Segment initialized, Bits 1-31: reserved)
  unsigned char  device_state;            // 10: Device State (0-5)
  unsigned char  bytes011_013[3];         // 11-13: reserved
  unsigned short ext_status_code;         // 14-15: Status of last SCT command (0xffff if executing)
  unsigned short action_code;             // 16-17: Action code of last SCT command
  unsigned short function_code;           // 18-19: Function code of last SCT command
  unsigned char  bytes020_039[20];        // 20-39: reserved
  uint64_t       lba_current;             // 40-47: LBA of SCT command executing in background
  unsigned char  bytes048_199[152];       // 48-199: reserved
  signed char    hda_temp;                // 200: Current temperature in Celsius (0x80 = invalid)
  signed char    min_temp;                // 201: Minimum temperature this power cycle
  signed char    max_temp;                // 202: Maximum temperature this power cycle
  signed char    life_min_temp;           // 203: Minimum lifetime temperature
  signed char    life_max_temp;           // 204: Maximum lifetime temperature
  unsigned char  byte205;                 // 205: reserved (T13/e06152r0-2: Average lifetime temperature)
  unsigned int   over_limit_count;        // 206-209: # intervals since last reset with temperature > Max Op Limit
  unsigned int   under_limit_count;       // 210-213: # intervals since last reset with temperature < Min Op Limit
  unsigned short smart_status;            // 214-215: LBA(32:8) of SMART RETURN STATUS (0, 0x2cf4, 0xc24f) (ACS-4)
  unsigned char  bytes216_479[479-216+1]; // 216-479: reserved
  unsigned char  vendor_specific[32];     // 480-511: vendor specific
} ATTR_PACKED;
#pragma pack()


////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////HDD thermal zone  related decl.../////////////////////////////////
#define DRIVER_NAME "hdd_tz_driver"
#define THERMAL_NAME "hdd"
#define SENSOR_NAME "hdd_sensor"

#define HHD_TEMP_CRIT 57000
#define HHD_TEMP_DEFAULT 0
#define HDD_THERMAL_PARAM_ALPHA  0
#define HDD_THERMAL_PARAM_OFFSET 0
#define HDD_THERMAL_PARAM_WEIGHT 0
#define HDD_MAX_REASONABLE_TEMP 80000

struct hdd_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct bcm_thermal_platform_data *pdata;
};

// Optimal operating temp. range for a typical HDD is from mid 30s to lower 40s
// and typically aging HDD tends to fail if temp. is over 40C.
static struct bcm_thermal_platform_data hdd_thermal_data = {
	.num_trips = 7,
	.mode = THERMAL_DEVICE_ENABLED,
	.polling_delay = 5000,
	.trips[0] = {.temp = 0, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 1, .lower = 1},},

	.trips[1] = {.temp = 45000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 3, .lower = 2},},

	.trips[2] = {.temp = 48000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 4, .lower = 3},},

	.trips[3] = {.temp = 51000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 5, .lower = 4},},

	.trips[4] = {.temp = 53000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 6, .lower = 5},},

	.trips[5] = {.temp = 55000, .type = THERMAL_TRIP_ACTIVE, .hyst = 1000,
                     .cdev[0] = { .type = "pwm-fan", .upper = 6, .lower = 6},},

	.trips[6] = {.temp = HHD_TEMP_CRIT, .type = THERMAL_TRIP_CRITICAL, .hyst = 1000},
};

////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////HDD thermal sensor abstraction...////////////////////////////////////////////
/*
* SCSI thermal device structure
* @scsi_dev - Context for sending cmds to the SCSI devcie
* @hdd_tzone - For HDD thermal zoee
* @temp - current temp
*/
struct scsi_thermal_device {
	struct scsi_device*        scsi_dev;
	struct mutex               device_mutex;
	struct scsi_ata_cmd_buffer cmd_buf;
	struct hdd_thermal_zone*   hdd_tzone;
	struct timespec            update_ts; //last update time stamp
	int                        temp;
	bool                       device_ready;
	bool                       device_not_supported;
	bool                       pcba; //facotory pcba test stage, no HDD installed
};

#endif //_HDD_THERMAL_
