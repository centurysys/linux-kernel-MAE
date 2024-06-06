/*
 * Copyright 2022 Morse Micro.
 *
 */

#include "mmrc_osal.h"

void osal_mmrc_seed_random(void)
{
#ifdef CONFIG_MORSE_RC
	prandom_seed(jiffies);
#else
	srand(time(NULL));
#endif
}

u32 osal_mmrc_random_u32(void)
{
#ifdef CONFIG_MORSE_RC
	return prandom_u32();
#else
	return rand();
#endif
}
