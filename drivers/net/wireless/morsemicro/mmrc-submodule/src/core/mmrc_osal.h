/*
 * Copyright 2022 Morse Micro.
 *
 */
#ifndef MMRC_OSAL_H__
#define MMRC_OSAL_H__

#ifdef CONFIG_MORSE_RC
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/random.h>
#include <linux/time.h>

#define BIT_COUNT(_x) (hweight_long(_x))

#ifndef MMRC_OSAL_ASSERT
#define MMRC_OSAL_ASSERT(_x) WARN_ON_ONCE(!(_x))
#endif

#ifndef MMRC_OSAL_PR_ERR
#define MMRC_OSAL_PR_ERR(...) pr_err(__VA_ARGS__)
#endif

#else

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <assert.h>

#include "mmrc_compat.h"

#define BIT_COUNT(_x) (__builtin_popcount(_x))

#ifndef MMRC_OSAL_ASSERT
#define MMRC_OSAL_ASSERT(_x) assert(_x)
#endif

#ifndef MMRC_OSAL_PR_ERR
#define MMRC_OSAL_PR_ERR(...) printf(__VA_ARGS__)
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif

/* TODO: We need to re-design debugfs somehow */

void osal_mmrc_seed_random(void);

/**
 * Function to retrieve a random 32bit number between 0 and @c max.
 *
 * @param max Maximum value (inclusive)
 *
 * @returns A randomly generated integer (0 <= i <= max).
 */
u32 osal_mmrc_random_u32(u32 max);

#endif /* MMRC_OSAL_H__ */
