/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/module.h>
#include <linux/stringify.h>
#include "debug.h"
#include "morse.h"

uint test_mode;
#ifdef CONFIG_MORSE_ENABLE_TEST_MODES
module_param(test_mode, uint, 0644);
MODULE_PARM_DESC(test_mode, "Enable test modes");
#endif

uint debug_mask = CONFIG_MORSE_DEBUG_MASK;
module_param(debug_mask, uint, 0644);

char serial[SERIAL_SIZE_MAX] = "default";
module_param_string(serial, serial, sizeof(serial), 0644);

char board_config_file[BCF_SIZE_MAX] = "";
module_param_string(bcf, board_config_file, sizeof(board_config_file), 0644);
MODULE_PARM_DESC(bcf, "BCF filename to load");

/* Verify OTP before using chip */
u8 enable_otp_check = 0x1;
module_param(enable_otp_check, byte, 0644);

static int __init morse_init(void)
{
	int ret = 0;

	pr_info("morse micro driver registration. Version %s\n", DRV_VERSION);

	/*
	 * Maintain backwards compatibility (for now)
	 * Start with most verbose level, i.e. LSB.
	 */
	if (debug_mask & 0x01)
		morse_init_log_levels(MORSE_MSG_DEBUG);
	else if (debug_mask & 0x02)
		morse_init_log_levels(MORSE_MSG_INFO);
	else if (debug_mask & 0x04)
		morse_init_log_levels(MORSE_MSG_WARN);
	else if (debug_mask & 0x08)
		morse_init_log_levels(MORSE_MSG_ERR);
	else
		morse_init_log_levels(MORSE_MSG_NONE);

#ifdef CONFIG_MORSE_SDIO
	ret = morse_sdio_init();
	if (ret)
		pr_err("morse_sdio_failed() failed: %d\n", ret);
#endif

#ifdef CONFIG_MORSE_SPI
	ret = morse_spi_init();
	if (ret)
		pr_err("morse_spi_failed() failed: %d\n", ret);
#endif


	return ret;
}

static void __exit morse_exit(void)
{
#ifdef CONFIG_MORSE_SDIO
	morse_sdio_exit();
#endif
#ifdef CONFIG_MORSE_SPI
	morse_spi_exit();
#endif

}

module_init(morse_init);
module_exit(morse_exit);

MODULE_AUTHOR("Morse Micro, Inc.");
MODULE_DESCRIPTION("Driver support for Morse Micro SDIO/SPI devices");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);
