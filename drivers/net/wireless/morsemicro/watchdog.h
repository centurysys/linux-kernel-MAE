/*
 * Copyright 2017-2022 Morse Micro
 */

#ifndef _MORSE_WATCHDOG_H_
#define _MORSE_WATCHDOG_H_

#include "morse.h"

typedef int (*watchdog_callback_t)(struct morse *);

/**
 * @brief Initialize a watchdog timer
 *
 * @param mors The global morse config object
 * @param interval_ms The parameter holds the timeout interval in seconds
 * @param ping The pointer to ping callback function
 * @param reset The pointer to reset callback function
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_init(struct morse *mors, int interval_ms,
			watchdog_callback_t ping, watchdog_callback_t reset);

/**
 * @brief Cancel an active watchdog timer and
 *        release allocated memory.
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_cleanup(struct morse *mors);

/**
 * @brief Start a watchdog timer
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_start(struct morse *mors);

/**
 * @brief Stop an active watchdog timer
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_stop(struct morse *mors);

/**
 * @brief Restart a watchdog timer expiry (now + interval)
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_refresh(struct morse *mors);

/**
 * @brief Temporarily pause the watchdog.
 *
 * This will suspend the watchdog timer until morse_watchdog_resume()
 * is invoked. There will be no further watchdog timeouts until the
 * watchdog is resumed. If the watchdog is stopped and restarted while
 * paused, it will still remain paused until resumed.
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_pause(struct morse *mors);

/**
 * @brief Resume the watchdog, if it was paused.
 *
 * This will resume operation of the watchdog timer following
 * morse_watchdog_pause(). The watchdog timer will be scheduled
 * for (now + interval).
 *
 * @param mors The global morse config object
 * @return int 0 if success, -error otherwise
 */
int morse_watchdog_resume(struct morse *mors);

/**
 * @brief Return a watchdog timeout interval in seconds
 *
 * @param mors The global morse config object
 * @return int interval > 0, -error otherwise
 */
int morse_watchdog_get_interval(struct morse *mors);

#endif /* !_MORSE_WATCHDOG_H_ */
