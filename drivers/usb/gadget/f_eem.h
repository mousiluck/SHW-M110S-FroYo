/*
 * Gadget Driver for Android ADB
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __F_EEM_H
#define __F_EEM_H

/*
 * set USBCV_CH9_REMOTE_WAKE_UP_TEST 1 ONLY for testing USBCV ch9
 * RemoteWakeupTestEnabled & RemoteWakeupTestDisabled
 * Confirm USBCV_CH9_REMOTE_WAKE_UP_TEST 
 * in s3c-udc-otg-hs.c and f_adb.h 
 */

// void eem_function_enable(int enable);
int eem_function_add(struct usb_composite_dev *cdev,
	struct usb_configuration *c);
int eem_function_config_changed(struct usb_composite_dev *cdev,
	struct usb_configuration *c);

#endif /* __F_ADB_H */
