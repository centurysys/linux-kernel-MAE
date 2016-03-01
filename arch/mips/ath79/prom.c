/*
 *  Atheros AR71XX/AR724X/AR913X specific prom routines
 *
 *  Copyright (C) 2015 Laurent Fasnacht <l@libres.ch>
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/initrd.h>

#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/fw/fw.h>

#include "common.h"

static char ath79_cmdline_buf[COMMAND_LINE_SIZE] __initdata;

static void __init ath79_prom_append_cmdline(const char *name,
					      const char *value)
{
	snprintf(ath79_cmdline_buf, sizeof(ath79_cmdline_buf),
		 " %s=%s", name, value);
	strlcat(arcs_cmdline, ath79_cmdline_buf, sizeof(arcs_cmdline));
}

void __init prom_init(void)
{
	const char *env;

	fw_init_cmdline();

	env = fw_getenv("ethaddr");
	if (env)
		ath79_prom_append_cmdline("ethaddr", env);

	env = fw_getenv("board");
	if (env) {
		/* Workaround for buggy bootloaders */
		if (strcmp(env, "RouterStation") == 0 ||
		    strcmp(env, "Ubiquiti AR71xx-based board") == 0)
			env = "UBNT-RS";

		if (strcmp(env, "RouterStation PRO") == 0)
			env = "UBNT-RSPRO";

		ath79_prom_append_cmdline("board", env);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/* Read the initrd address from the firmware environment */
	initrd_start = fw_getenvl("initrd_start");
	if (initrd_start) {
		initrd_start = KSEG0ADDR(initrd_start);
		initrd_end = initrd_start + fw_getenvl("initrd_size");
	}
#endif
}

void __init prom_free_prom_memory(void)
{
	/* We do not have to prom memory to free */
}
