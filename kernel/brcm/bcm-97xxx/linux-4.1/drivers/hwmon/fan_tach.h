/*
 * Fan Tachometer driver.
 *
 * This module handles interrupt form the Fan Tachometer driven GPIO and
 * calculated the fan speed in RPM.
 *
 * Copyright (C) 2017 Lab126
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _FAN_TACH_
#define _FAN_TACH_
 
int get_fan_rpm(void);

#endif //_FAN_TACH_
