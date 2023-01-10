/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2020, Century Systems <kikuchi@centurysys.co.jp>
 */

#ifndef __LINUX_POWER_RESET_AT91_SAMA5D2_SHDWC_H__
#define __LINUX_POWER_RESET_AT91_SAMA5D2_SHDWC_H__

#ifdef CONFIG_POWER_RESET_AT91_SAMA5D2_SHDWC_CONFIGURABLE
int at91_shdwc_get_wakeup(unsigned int pin, unsigned *type);
int at91_shdwc_set_wakeup(unsigned int pin, unsigned type);
#endif

#endif
