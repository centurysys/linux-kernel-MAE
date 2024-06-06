#ifndef _MORSE_OF_H_
#define _MORSE_OF_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include "hw.h"

/**
 * morse_of_probe - reads of pins in compatible device tree.
 * @dev: device struct containing the of_node
 * @cfg: morse_hw_config struct to be updated from of_node
 * @match_table: match table containing the compatibility strings
 */
void morse_of_probe(struct device *dev, struct morse_hw_cfg *cfg,
		    const struct of_device_id *match_table);

#endif /* !_MORSE_OF_H_ */
