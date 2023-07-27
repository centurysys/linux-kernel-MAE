/*
 * Copyright 2022 Morse Micro
 */

#pragma once

/* Bitmap utilities */
#define BMGET(_v, _f)     (((_v) & (_f)) >> __builtin_ctz(_f))
#define BMSET(_v, _f)     (((_v) << __builtin_ctz(_f)) & (_f))

#define ROUND_BYTES_TO_WORD(_nbytes) ((_nbytes + 3) & ~0x03)
