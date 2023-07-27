/*
 * Copyright 2022 Morse Micro.
 *
 */
#ifndef MMRC_OSAL_H__
#define MMRC_OSAL_H__

#ifdef CONFIG_MORSE_RC
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/random.h>
#include <linux/time.h>

typedef u16 uint16_t;
typedef u32 uint32_t;
typedef s32 int32_t;

#define BIT_COUNT(x) (hweight_long(x))

#else

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define BIT_COUNT(x) (__builtin_popcount(x))

#endif

/* TODO: We need to re-design debugfs somehow */

void osal_mmrc_seed_random(void);
uint32_t osal_mmrc_random_u32(void);

#endif /* MMRC_OSAL_H__ */
