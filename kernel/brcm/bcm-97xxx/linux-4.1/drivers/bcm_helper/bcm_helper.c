/*
 *
 *  Copyright (C) 2017, Broadcom Corporation. All Rights Reserved.
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *  SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 *  OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include "bcm_helper.h"

/*
 * Store the BRCM Licensed symbols
 */
static struct brcm_helper_info brcm_helper;

/*
 * BRCM function to register callbacks
 */
int
register_brcm_helper(int (*brcm_tempsense)(int, int *))
{
	brcm_helper.tempsense = brcm_tempsense;

	return 0;
}
EXPORT_SYMBOL(register_brcm_helper); /* Register BRCM call-backs */

/*
 * BRCM function to unregister callbacks
 */
int
unregister_brcm_helper(void)
{
	brcm_helper.tempsense = NULL;

	return 0;
}
EXPORT_SYMBOL(unregister_brcm_helper); /* unregister BRCM call-backs */

/*
 * Generic function to be used by Kernel thermal framework
 */
int
get_wifi_tempsense(int chip_id, int *ret_phytemp_ptr)
{
	if (brcm_helper.tempsense)
		return brcm_helper.tempsense(chip_id, ret_phytemp_ptr);
	else
		return -1;
}
EXPORT_SYMBOL(get_wifi_tempsense); /* Read WiFi device temperature */

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
