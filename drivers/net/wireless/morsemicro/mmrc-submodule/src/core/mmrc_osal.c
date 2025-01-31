/*
 * Copyright 2022 Morse Micro.
 *
 */

#include "mmrc_osal.h"

void osal_mmrc_seed_random(void)
{
#ifdef CONFIG_MORSE_RC
#if KERNEL_VERSION(5, 19, 0) > LINUX_VERSION_CODE
	prandom_seed(jiffies);
#else
    /* We do nothing here for upstream kernel */
#endif
#else
	srand(time(NULL));
#endif
}

u32 osal_mmrc_random_u32(u32 max)
{
#ifdef CONFIG_MORSE_RC
#if KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE
	return prandom_u32_max(max);
#else
	return get_random_u32_below(max);
#endif
#else
	return rand() % max;
#endif
}
