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

#ifndef _bcm_helper_h_
#define _bcm_helper_h_

#define BCM4352_CHIP_ID  0x4352  /* 4352  chip id */
#define BCM43217_CHIP_ID 43217   /* 43217 chip id */

struct brcm_helper_info
{
	int (*tempsense)(int chip_id, int *ret_ptr);
};

int get_wifi_tempsense(int chip_id, int *ret_phytemp_ptr);
int register_brcm_helper(int (*brcm_tempsense)(int, int *));
int unregister_brcm_helper(void);

#endif /* _bcm_helper_h_ */
