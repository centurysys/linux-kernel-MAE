#ifndef _MORSE_PS_H_
#define _MORSE_PS_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include "morse.h"

/** This should be nominally <= the dynamic ps timeout */
#define NETWORK_BUS_TIMEOUT_MS (90)
/** The default period of time to wait to re-evaluate powersave */
#define DEFAULT_BUS_TIMEOUT_MS (5)

int morse_ps_enable(struct morse *mors);

int morse_ps_disable(struct morse *mors);

/**
 * Call this function when there is activity on the bus that should
 * delay the driver in disabling the bus.
 *
 * @mors: Morse chip instance
 * @timeout_ms: The timeout from now to add (ms)
 */
void morse_ps_bus_activity(struct morse *mors, int timeout_ms);

int morse_ps_init(struct morse *mors, bool enable, bool enable_dynamic_ps);

void morse_ps_finish(struct morse *mors);

#endif /* !_MORSE_PS_H_ */
