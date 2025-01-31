/*
 * Copyright 2023 Morse Micro
 */

/*
 * This header file is used to hide elements that must be used but would cause
 * the checkpatch utility to complain.
 */

#pragma once

#ifndef CONFIG_MORSE_RC		/* Not building in the Linux kernel */

/* Checkpatch does not like these typedefs, which are used by the test harness */
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

#endif
