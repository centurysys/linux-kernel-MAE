#ifndef _MORSE_WIPHY_H_
#define _MORSE_WIPHY_H_

/*
 * Copyright 2017-2023 Morse Micro
 *
 */
#include <net/mac80211.h>

/**
 * morse_wiphy_to_morse -  Look up &struct mors inside &struct wiphy
 *
 * Return: pointer to &struct mors
 */
struct morse *morse_wiphy_to_morse(struct wiphy *wiphy);


#endif /* !_MORSE_WIPHY_H_ */
