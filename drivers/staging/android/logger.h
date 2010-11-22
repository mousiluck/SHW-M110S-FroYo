/* include/linux/logger.h
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Author: Robert Love <rlove@android.com>
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

#ifndef _LINUX_LOGGER_H
#define _LINUX_LOGGER_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct logger_entry {
	__u16		len;	/* length of the payload */
	__u16		__pad;	/* no matter what, we get 2 bytes of padding */
	__s32		pid;	/* generating process's pid */
	__s32		tid;	/* generating process's tid */
	__s32		sec;	/* seconds since Epoch */
	__s32		nsec;	/* nanoseconds */
	char		msg[0];	/* the entry's payload */
};

#define LOGGER_LOG_RADIO	"log_radio"	/* radio-related messages */
#define LOGGER_LOG_EVENTS	"log_events"	/* system/hardware events */
#define LOGGER_LOG_SYSTEM	"log_system"	/* system/framework messages */
#define LOGGER_LOG_MAIN		"log_main"	/* everything else */
// msjeong DH10 : Audio & PV log dump (FEATURE_SEC_KOREA_MM_PV_LOG) [[--
#define LOGGER_LOG_AUDIO	"log_audio"	/* audio-related messages */
#define LOGGER_LOG_PV		"log_pv"	/* pv player message : noik.heo (20100316) */
// msjeong DH10 : Audio & PV log dump --]]

#define LOGGER_ENTRY_MAX_LEN		(4*1024)
#define LOGGER_ENTRY_MAX_PAYLOAD	\
	(LOGGER_ENTRY_MAX_LEN - sizeof(struct logger_entry))

#define __LOGGERIO	0xAE

#define LOGGER_GET_LOG_BUF_SIZE		_IO(__LOGGERIO, 1) /* size of log */
#define LOGGER_GET_LOG_LEN		_IO(__LOGGERIO, 2) /* used log len */
#define LOGGER_GET_NEXT_ENTRY_LEN	_IO(__LOGGERIO, 3) /* next entry len */
#define LOGGER_FLUSH_LOG		_IO(__LOGGERIO, 4) /* flush log */
// msjeong DH10 : Audio & PV log dump (FEATURE_SEC_KOREA_MM_PV_LOG) [[--
#define LOGGER_GET_LOGCONFIG    _IO(__LOGGERIO, 5)
#define LOGGER_SET_LOGCONFIG_RESET    _IO(__LOGGERIO, 6)
#define LOGGER_SET_LOGCONFIG_ALL    _IO(__LOGGERIO, 7)
#define LOGGER_SET_LOGCONFIG_RIL    _IO(__LOGGERIO, 8)
#define LOGGER_SET_LOGCONFIG_AUDIO    _IO(__LOGGERIO, 9)
#define LOGGER_SET_LOGCONFIG_PV    _IO(__LOGGERIO, 10)
// msjeong DH10 : Audio & PV log dump --]]

#endif /* _LINUX_LOGGER_H */
