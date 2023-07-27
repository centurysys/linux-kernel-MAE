/*
 * Copyright 2022 Morse Micro
 *
 */
#ifndef _MORSE_UTILS_H_
#define _MORSE_UTILS_H_

/** Integer ceiling function */
#define MORSE_INT_CEIL(_num, _div)	(((_num) + (_div) - 1) / (_div))

/** Convert from a time in us to time units (1024us) */
#define MORSE_US_TO_TU(x)		((x) / 1024)

/** Convert from a time in time units (1024us) to us */
#define MORSE_TU_TO_US(x)		((x) * 1024)

#endif  /* !_MORSE_UTILS_H_ */
