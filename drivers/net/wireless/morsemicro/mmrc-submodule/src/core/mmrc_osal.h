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

#define BIT_COUNT(x) (hweight_long(x))

#ifndef MMRC_OSAL_ASSERT
#define MMRC_OSAL_ASSERT(x) BUG_ON(!(x))
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

#define BIT_COUNT(x) (__builtin_popcount(x))

#ifndef MMRC_OSAL_ASSERT
#define MMRC_OSAL_ASSERT(x) assert(x)
#endif

#ifndef MMRC_OSAL_PR_ERR
#define MMRC_OSAL_PR_ERR(...) printf(__VA_ARGS__)
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#endif

/* TODO: We need to re-design debugfs somehow */

void osal_mmrc_seed_random(void);
u32 osal_mmrc_random_u32(void);

#endif /* MMRC_OSAL_H__ */
