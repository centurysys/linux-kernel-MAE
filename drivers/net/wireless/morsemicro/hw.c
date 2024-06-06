/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/gpio.h>

#include "morse.h"
#include "debug.h"
#include "hw.h"
#include "pager_if.h"
#include "bus.h"

int morse_hw_irq_enable(struct morse *mors, u32 irq, bool enable)
{
	u32 irq_en, irq_en_addr = irq < 32 ? MORSE_REG_INT1_EN(mors) : MORSE_REG_INT2_EN(mors);
	u32 irq_clr_addr = irq < 32 ? MORSE_REG_INT1_CLR(mors) : MORSE_REG_INT2_CLR(mors);
	u32 mask = irq < 32 ? (1 << irq) : (1 << (irq - 32));

	morse_claim_bus(mors);
	morse_reg32_read(mors, irq_en_addr, &irq_en);
	if (enable)
		irq_en |= (mask);
	else
		irq_en &= ~(mask);
	morse_reg32_write(mors, irq_clr_addr, mask);
	morse_reg32_write(mors, irq_en_addr, irq_en);
	morse_release_bus(mors);

	return 0;
}

int morse_hw_irq_handle(struct morse *mors)
{
	u32 status1 = 0;
#if defined(CONFIG_MORSE_DEBUG_IRQ)
	int i;
#endif

	morse_claim_bus(mors);
	morse_reg32_read(mors, MORSE_REG_INT1_STS(mors), &status1);
	if (status1 & MORSE_CHIP_IF_IRQ_MASK_ALL)
		mors->cfg->ops->chip_if_handle_irq(mors, status1);
	if (status1 & MORSE_INT_BEACON_VIF_MASK_ALL)
		morse_beacon_irq_handle(mors, status1);
	if (status1 & MORSE_INT_NDP_PROBE_REQ_PV0_MASK)
		morse_ndp_probe_req_resp_irq_handle(mors);
	morse_reg32_write(mors, MORSE_REG_INT1_CLR(mors), status1);
	morse_release_bus(mors);

#if defined(CONFIG_MORSE_DEBUG_IRQ)
	mors->debug.hostsync_stats.irq++;
	for (i = 0; i < ARRAY_SIZE(mors->debug.hostsync_stats.irq_bits); i++) {
		if (status1 & BIT(i))
			mors->debug.hostsync_stats.irq_bits[i]++;
	}
#endif

	return status1 ? 1 : 0;
}

int morse_hw_irq_clear(struct morse *mors)
{
	morse_claim_bus(mors);
	morse_reg32_write(mors, MORSE_REG_INT1_CLR(mors), 0xFFFFFFFF);
	morse_reg32_write(mors, MORSE_REG_INT2_CLR(mors), 0xFFFFFFFF);
	morse_release_bus(mors);
	return 0;
}

int morse_hw_reset(int reset_pin)
{
	int ret = gpio_request(reset_pin, "morse-reset-ctrl");

	if (ret < 0) {
		MORSE_PR_ERR(FEATURE_ID_DEFAULT, "Failed to acquire reset gpio. Skipping reset.\n");
		return ret;
	}

	pr_info("Resetting Morse Chip\n");
	gpio_direction_output(reset_pin, 0);
	mdelay(20);
	/* setting gpio as float to avoid forcing 3.3V High */
	gpio_direction_input(reset_pin);
	pr_info("Done\n");

	gpio_free(reset_pin);

	return ret;
}

bool is_otp_xtal_wait_supported(struct morse *mors)
{
	int ret;
	u32 otp_word2;
	u32 otp_xtal_wait;

	if (MORSE_REG_OTP_DATA_WORD(mors, 0) == 0)
		/* Device doesn't support OTP (probably an FPGA) */
		return true;

	if (MORSE_REG_OTP_DATA_WORD(mors, 2) != 0) {
		morse_claim_bus(mors);
		ret = morse_reg32_read(mors, MORSE_REG_OTP_DATA_WORD(mors, 2), &otp_word2);
		morse_release_bus(mors);
		if (ret < 0) {
			MORSE_ERR(mors, "OTP data2 value read failed: %d\n", ret);
			return false;
		}
		otp_xtal_wait = (otp_word2 & MM610X_OTP_DATA2_XTAL_WAIT_POS);
		if (!otp_xtal_wait) {
			ret = -1;
			MORSE_ERR(mors, "OTP xtal wait bits not set\n");
			return false;
		}
		return true;
	}
	return false;
}

bool morse_hw_is_valid_chip_id(u32 chip_id, u32 *valid_chip_ids)
{
	int i;

	BUG_ON(chip_id == CHIP_ID_END);

	for (i = 0; valid_chip_ids[i] != CHIP_ID_END; i++)
		if (chip_id == valid_chip_ids[i])
			return true;
	return false;
}
