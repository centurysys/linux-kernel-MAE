/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/pm_runtime.h>

#include "morse.h"
#include "bus.h"
#include "mac.h"
#include "firmware.h"
#include "debug.h"
#include "of.h"

#ifdef CONFIG_MORSE_USER_ACCESS
#include "uaccess.h"
#endif

#define MORSE_DMA_BUFFER_SIZE (32 * 1024)

#define MORSE_SDIO_DEVICE(vend, dev, cfg) \
	.class = SDIO_ANY_ID, \
	.vendor = (vend), .device = (dev), \
	.driver_data = (kernel_ulong_t)&(cfg)

#define SDIO_CLK_DEBUGFS_MAX  128
char sdio_clk_debugfs[SDIO_CLK_DEBUGFS_MAX] = "";
module_param_string(sdio_clk_debugfs, sdio_clk_debugfs, sizeof(sdio_clk_debugfs), 0644);

/** Minimum string length for the path of SDIO-CLK switching */
#define MIN_STRLEN_SDIO_CLK_PATH 20

/** Power management runtime autosuspend_delay value in milliseconds */
#define PM_RUNTIME_AUTOSUSPEND_DELAY_MS 50

/**
 * Put the SDIO-clk back to where it was before. If SDIO-clk is set to 42MHz in
 * boot/config.txt then it won't exceed 42MHz. The SDIO-clk will be 42MHz.
 **/
#define FAST_SDIO_CLK_HZ 50000000

/* Slow down SDIO CLK to 150KHz. This is the lowest value we can set. */
#define SLOW_SDIO_CLK_HZ 150000

struct morse_sdio {
	bool enabled;
	u32 bulk_addr_base;
	u32 register_addr_base;
	struct sdio_func *func;
	const struct sdio_device_id *id;

	u8 *dma_buffer;

	/* protects access to dma_buffer */
	struct mutex dma_buffer_mutex;
};

#ifdef CONFIG_MORSE_USER_ACCESS
struct uaccess *morse_uaccess;
#endif

static void morse_sdio_remove(struct sdio_func *func);

static u32 morse_sdio_calculate_base_address(u32 address, u8 access)
{
	return (address & MORSE_SDIO_RW_ADDR_BOUNDARY_MASK) | (access & 0x3);
}

static int morse_sdio_set_func_address_base(struct morse_sdio *sdio,
					u32 address, u8 access, bool bulk)
{
	int	ret = 0;
	u8	base[4];
	u32	calculated_addr_base = morse_sdio_calculate_base_address(address, access);
	u32	*current_addr_base = bulk ? &sdio->bulk_addr_base : &sdio->register_addr_base;
	struct sdio_func *func2 = sdio->func;
	struct sdio_func *func1 = sdio->func->card->sdio_func[0];
	struct sdio_func *func_to_use = bulk ? func2 : func1;
	struct morse *mors = sdio_get_drvdata(sdio->func);
	int retries = 0;
	static const int max_retries = 3;

	if ((*current_addr_base) == calculated_addr_base)
		return ret;

	base[0] = (u8)((address & 0x00FF0000) >> 16);
	base[1] = (u8)((address & 0xFF000000) >> 24);
	base[2] = access & 0x3; /* 1, 2 or 4 byte access */

retry:
	/* Write them as single bytes for now */
	if (base[0] != (u8)(((*current_addr_base) & 0x00FF0000) >> 16)) {
		sdio_writeb(func_to_use, base[0], MORSE_REG_ADDRESS_WINDOW_0, &ret);
		if (ret)
			goto err;
	}

	if (base[1] != (u8)(((*current_addr_base) & 0xFF000000) >> 24)) {
		sdio_writeb(func_to_use, base[1], MORSE_REG_ADDRESS_WINDOW_1, &ret);
		if (ret)
			goto err;
	}

	if (base[2] != (u8)(((*current_addr_base) & 0x3))) {
		sdio_writeb(func_to_use, base[2], MORSE_REG_ADDRESS_CONFIG, &ret);
		if (ret)
			goto err;
	}

	/**
	 * TODO: The following can give us a speed boost, but it is risky.
	 *
	 * One of the bytes you are writing sets the size of our memory access,
	 * it is byte, half word or word. This is used for
	 * extended IO (i.e using CMD53). The memcpy_toio is internally
	 * using CMD53. So you are accessing the memory before
	 * setting the access size.
	 *
	 * ret = sdio_memcpy_toio(sdio->func, MORSE_REG_ADDRESS_WINDOW_0, base, 3);
	 */

	*current_addr_base = calculated_addr_base;
	if (retries)
		morse_info(mors, "%s succeeded after %d retries\n", __func__, retries);
	return ret;
err:
	retries++;
	if ((ret == -ETIMEDOUT) && (retries <= max_retries)) {
		morse_info(mors, "%s failed (%d), retrying (%d/%d)\n", __func__, ret, retries, max_retries);
		goto retry;
	}

	morse_err(mors, "%s %d\n", __func__, ret);
	return ret;
}

static struct sdio_func *morse_sdio_get_func(struct morse_sdio *sdio,
					u32 address, ssize_t size, u8 access)
{
	int	ret = 0;
	u32 calculated_base_address = morse_sdio_calculate_base_address(address, access);
	struct sdio_func *func2 = sdio->func;
	struct sdio_func *func1 = sdio->func ? sdio->func->card->sdio_func[0] : NULL;
	struct morse *mors = sdio->func ? sdio_get_drvdata(sdio->func) : NULL;
	struct sdio_func *func_to_use;

	if (mors == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	/* Order matters here, please don't re-order */
	if (size > sizeof(u32)) {
		ret = morse_sdio_set_func_address_base(sdio, address, access, true);
		MORSE_WARN_ON(sdio->bulk_addr_base == 0);
		func_to_use = func2;
	} else if ((sdio->bulk_addr_base == calculated_base_address) && (func2 != NULL))
		func_to_use = func2;
	else if (func1 != NULL) {
		ret = morse_sdio_set_func_address_base(sdio, address, access, false);
		MORSE_WARN_ON(sdio->register_addr_base == 0);
		func_to_use = func1;
	} else {
		ret = morse_sdio_set_func_address_base(sdio, address, access, true);
		MORSE_WARN_ON(sdio->bulk_addr_base == 0);
		func_to_use = func2;
	}

exit:
	if (ret)
		morse_err(mors, "%s failed\n", __func__);

	return ret ? NULL : func_to_use;
}

static int morse_sdio_regl_write(struct morse_sdio *sdio,
					u32 address, u32 value, u8 claim)
{
	ssize_t ret = 0;
	struct morse *mors = sdio->func ? sdio_get_drvdata(sdio->func) : NULL;
	u32 original_address = address;
	struct sdio_func *func_to_use;

	if (mors == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	func_to_use = morse_sdio_get_func(sdio, address, sizeof(u32), MORSE_CONFIG_ACCESS_4BYTE);
	if (func_to_use == NULL) {
		ret = -EIO;
		goto exit;
	}

	address &= 0x0000FFFF;	/* remove base and keep offset */
	sdio_writel(func_to_use, value, address, (int *)&ret);

	/* return written size */
	if (ret)
		morse_err(mors, "sdio writel failed %zu", ret);
	else
		ret = sizeof(value);

	if (original_address == MORSE_REG_RESET(mors) &&
		value == MORSE_REG_RESET_VALUE(mors)) {
		morse_dbg(mors, "SDIO reset detected, invalidating base addr\n");
		sdio->bulk_addr_base = 0x0;
		sdio->register_addr_base = 0x0;
	}
exit:
	return (int)ret;
}

static int morse_sdio_regl_read(struct morse_sdio *sdio,
					u32 address, u32 *value, u8 claim)
{
	ssize_t ret = 0;
	struct morse *mors = sdio->func ? sdio_get_drvdata(sdio->func) : NULL;
	struct sdio_func *func_to_use;

	if (mors == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	func_to_use = morse_sdio_get_func(sdio, address, sizeof(u32), MORSE_CONFIG_ACCESS_4BYTE);
	if (func_to_use == NULL) {
		ret = -EIO;
		goto exit;
	}

	address &= 0x0000FFFF;	/* remove base and keep offset */
	*value = sdio_readl(func_to_use, address, (int *)&ret);
	/* return read size */
	if (ret)
		morse_err(mors, "sdio readl failed %zu\n", ret);
	else
		ret = sizeof(*value);
exit:
	return (int)ret;
}

static inline bool buf_needs_bounce(u8 *buf)
{
	return ((unsigned long) buf & 0x3) || !virt_addr_valid(buf);
}

static int morse_sdio_mem_write(struct morse_sdio *sdio, u32 address,
				u8 *data, ssize_t size, u8 claim)
{
	ssize_t ret = 0;
	struct morse *mors = sdio->func ? sdio_get_drvdata(sdio->func) : NULL;
	int access = (size & 0x03) ?
			MORSE_CONFIG_ACCESS_1BYTE : MORSE_CONFIG_ACCESS_4BYTE;
	struct sdio_func *func_to_use;

	if (mors == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	func_to_use = morse_sdio_get_func(sdio, address, size, access);
	if (func_to_use == NULL) {
		ret = -EIO;
		goto exit;
	}

	address &= 0x0000FFFF;	/* remove base and keep offset */
	if (access == MORSE_CONFIG_ACCESS_4BYTE) {
		u8  *tbuf = NULL;
		bool bounced = false;

		/* An unaligned source address may cause some SDIO drivers to lockup
		 * upon execution of the DMA. To avoid this, we memcpy the data
		 * to a word-aligned scratch buffer and use that as the source address.
		 *
		 * In future, we may want to find a way to force memory alignment from
		 * the kernel to avoid the extra copy.
		 */
		if (buf_needs_bounce(data)) {
			BUG_ON(size > MORSE_DMA_BUFFER_SIZE);
			mutex_lock(&sdio->dma_buffer_mutex);
			tbuf = sdio->dma_buffer;
			memcpy(tbuf, data, size);
			bounced = true;
		} else {
			tbuf = data;
		}
		/* Use ex write */
		ret = sdio_memcpy_toio(func_to_use, address, tbuf, size);
		if (bounced)
			mutex_unlock(&sdio->dma_buffer_mutex);

		if (ret) {
			morse_err(mors, "sdio_memcpy_toio failed: %zu\n", ret);
			goto exit;
		}
	} else {
		int i;

		for (i = 0; i < size; i++) {
			sdio_writeb(func_to_use, data[i], address + i, (int *)&ret);
			if (ret) {
				morse_err(mors, "sdio_writeb failed: %zu\n",
					  ret);
				goto exit;
			}
		}
	}
	ret = size;
exit:
	return ret;
}

void morse_sdio_claim_host(struct morse *mors)
{
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;

	sdio_claim_host(func);
}

void morse_sdio_release_host(struct morse *mors)
{
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;

	sdio_release_host(func);
}

static int morse_sdio_mem_read(struct morse_sdio *sdio, u32 address,
			       u8 *data, ssize_t size, u8 claim)
{
	ssize_t ret = 0;
	struct morse *mors = sdio->func ? sdio_get_drvdata(sdio->func) : NULL;
	int access = (size & 0x03) ?
			MORSE_CONFIG_ACCESS_1BYTE : MORSE_CONFIG_ACCESS_4BYTE;
	struct sdio_func *func_to_use;

	if (mors == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	func_to_use = morse_sdio_get_func(sdio, address, size, access);
	if (func_to_use == NULL) {
		ret = -EIO;
		goto exit;
	}

	address &= 0x0000FFFF;	/* remove base and keep offset */
	if (access == MORSE_CONFIG_ACCESS_4BYTE) {
		u8  *tbuf = NULL;
		bool bounced = false;

		/* See `morse_sdio_mem_write` for more info on bouncing */
		if (buf_needs_bounce(data)) {
			BUG_ON(size > MORSE_DMA_BUFFER_SIZE);
			mutex_lock(&sdio->dma_buffer_mutex);
			tbuf = sdio->dma_buffer;
			bounced = true;
		} else {
			tbuf = data;
		}
		ret = sdio_memcpy_fromio(func_to_use, tbuf, address, size);
		if (bounced) {
			if (!ret)
				memcpy(data, tbuf, size);
			mutex_unlock(&sdio->dma_buffer_mutex);
		}
		if (ret) {
			morse_err(mors, "sdio_memcpy_fromio failed: %zu\n", ret);
			goto exit;
		}

		/* SW-3875: seems like sdio read can sometimes go wrong and read first 4-bytes word twice,
		 * overwriting second word (hence, tail will be overwritten with 'sync' byte). It seems
		 * like reading those again will fetch the correct word. Let's do that.
		 * NB: if been corrupted again, pass it anyway and upper layers wil handle it
		 */
		if (memcmp(data, data+4, 4) == 0) {
			/* sdio_memcpy_fromio corrupts first word. Lets try one more time before passing up */
			sdio_memcpy_fromio(func_to_use, data, address, 8);
		}
	} else {
		int i;

		for (i = 0; i < size; i++) {
			data[i] = sdio_readb(func_to_use, address + i, (int *)&ret);
			if (ret) {
				morse_err(mors, "sdio_readb failed: %zu\n", ret);
				goto exit;
			}
		}
	}
	ret = size;
exit:
	return ret;
}

static bool morse_sdio_operation_within_boundary(u32 address, u32 len)
{
	/* We can only write within a 64K boundary */
	return (address & MORSE_SDIO_RW_ADDR_BOUNDARY_MASK) ==
		((address + len) & MORSE_SDIO_RW_ADDR_BOUNDARY_MASK);
}

static int morse_sdio_dm_write(struct morse *mors, u32 address,
					const u8 *data, u32 len)
{
	ssize_t ret = 0;
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	if (morse_sdio_operation_within_boundary(address, len)) {
		ret = morse_sdio_mem_write(sdio, address, (u8 *)data, len, 1);
	} else {
		/* Write up to that boundary */
		u32 offset = ((address + len) &
			MORSE_SDIO_RW_ADDR_BOUNDARY_MASK);
		ssize_t first_write_len = (offset - address);
		ssize_t second_write_len = (len - first_write_len);

		ret = morse_sdio_mem_write(sdio, address,
			(u8 *) data, first_write_len, 1);

		if (ret != first_write_len)
			goto exit;

		ret = morse_sdio_mem_write(sdio, offset,
			(u8 *) data + first_write_len, second_write_len, 1);

		if (ret != second_write_len)
			goto exit;

		ret = first_write_len + second_write_len;
	}

	if (ret == len)
		return 0;

exit:
	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

static int morse_sdio_dm_read(struct morse *mors, u32 address,
						  u8 *data, u32 len)
{
	ssize_t ret = 0;
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	MORSE_WARN_ON(len % 4);

	if (morse_sdio_operation_within_boundary(address, len))
		ret = morse_sdio_mem_read(sdio, address, data, len, 1);
	else {
		/* Read up to that boundary */
		u32 offset = ((address + len) &
			MORSE_SDIO_RW_ADDR_BOUNDARY_MASK);
		ssize_t first_read_len = (offset - address);
		ssize_t second_read_len = (len - first_read_len);

		ret = morse_sdio_mem_read(sdio, address,
			(u8 *) data, first_read_len, 1);

		if (ret != first_read_len)
			goto exit;

		ret = morse_sdio_mem_read(sdio, offset,
			(u8 *) data + first_read_len, second_read_len, 1);

		if (ret != second_read_len)
			goto exit;

		ret = first_read_len + second_read_len;
	}

	if (ret == len)
		return 0;

exit:
	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

static int morse_sdio_reg32_write(struct morse *mors, u32 address, u32 val)
{
	ssize_t ret = 0;
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	ret = morse_sdio_regl_write(sdio, address, val, 1);
	if (ret == sizeof(val))
		return 0;
	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

static int morse_sdio_reg32_read(struct morse *mors, u32 address, u32 *val)
{
	ssize_t ret = 0;
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	ret = morse_sdio_regl_read(sdio, address, val, 1);
	if (ret == sizeof(*val))
		return 0;

	morse_err(mors, "%s failed %zu\n", __func__, ret);
	return -EIO;
}

/**
 * MM-5188 : Set the sdio clk to lowest 150KHz when disabling the sdio. And resume the sdio clk
 * when eanabling it.
 *
 * When the chip enters into sleep then MM input SDIO-CLK pad is not going into high-z and the
 * sdio-clk from driver is consistently running in high speed. As a result it leaks more IO
 * current. Reducing sdio-clk will reduce the IO leakge.
 **/
static void morse_sdio_clk_freq_switch(struct morse *mors, u64 sdio_clk_hz)
{
	int ret;
	static const char *const envp[] = {"HOME=/", NULL};
	char cmd[128];

	char *const argv[] = {"/bin/bash", "-c", cmd, NULL};

	if (strlen(sdio_clk_debugfs) <= MIN_STRLEN_SDIO_CLK_PATH) {
		morse_dbg(mors, "SDIO clock switching not supported, Skip.\n");

		return;
	}
	snprintf(cmd, sizeof(cmd), "echo %lld > %s", sdio_clk_hz, sdio_clk_debugfs);

	ret = call_usermodehelper(argv[0], (char **)argv, (char **)envp, UMH_WAIT_PROC);

	if (ret)
		morse_err(mors, "%s: Failed to switch SDIO-CLK to %lldHz (errno=%d)\n", __func__, sdio_clk_hz, ret);
	else
		morse_dbg(mors, "%s: SDIO-CLK switched to %lldHz\n", __func__, sdio_clk_hz);
}

static void morse_sdio_bus_enable(struct morse *mors, bool enable)
{
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;
	struct mmc_host *host = func->card->host;

	if (enable) {
		// No need to do anything special to re-enable the sdio bus
		// will happen automatically when a read/write is attempted
		// and sdio->bulk_addr_base == 0.
		sdio_claim_host(func);

		sdio->enabled = true;
		host->ops->enable_sdio_irq(host, 1);
		morse_dbg(mors, "%s: enabling bus\n", __func__);

		/* Make sure the card will not be powered off by runtime PM */
		pm_runtime_get_sync(&func->dev);

		sdio_release_host(func);

		/* Resume the SDIO-CLK speed */
		morse_sdio_clk_freq_switch(mors, FAST_SDIO_CLK_HZ);
	} else {
		/* Clear the address base so that we'll set it again later */
		sdio_claim_host(func);

		host->ops->enable_sdio_irq(host, 0);
		sdio->bulk_addr_base = 0;
		sdio->register_addr_base = 0;
		sdio->enabled = false;
		morse_dbg(mors, "%s: disabling bus\n", __func__);

		/* Let runtime PM know the card is powered off */
		pm_runtime_put_sync(&func->dev);

		sdio_release_host(func);

		/* Slow down SDIO-CLK to save chip IO's power */
		morse_sdio_clk_freq_switch(mors, SLOW_SDIO_CLK_HZ);
	}
}

static int morse_sdio_reset(int reset_pin, struct sdio_func *func)
{
	int ret = 0;
	struct mmc_card *card = func->card;

	/* reset the adapter */
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	/* Let runtime PM know the card is powered off */
	pm_runtime_put(&card->dev);

	morse_hw_reset(reset_pin);
	mdelay(20);

	sdio_claim_host(func);
	sdio_disable_func(func);
	mmc_hw_reset(func->card->host);
	sdio_enable_func(func);
	sdio_release_host(func);

	return ret;
}

static int morse_sdio_bus_reset(struct morse *mors)
{
	int ret = 0;
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;
	struct sdio_func *func = sdio->func;


	morse_sdio_remove(func);

	return ret;
}

static const struct morse_bus_ops morse_sdio_ops = {
	.dm_read = morse_sdio_dm_read,
	.dm_write = morse_sdio_dm_write,
	.reg32_read = morse_sdio_reg32_read,
	.reg32_write = morse_sdio_reg32_write,
	.set_bus_enable = morse_sdio_bus_enable,
	.claim = morse_sdio_claim_host,
	.release = morse_sdio_release_host,
	.reset = morse_sdio_bus_reset,
	.set_irq = morse_sdio_set_irq,
};

static void morse_sdio_irq_handler(struct sdio_func *func1)
{

	int ret = 0;
	struct sdio_func *func = func1->card->sdio_func[1];
	struct morse *mors = sdio_get_drvdata(func);
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	MORSE_WARN_ON(mors == NULL);

	(void)sdio;

	ret = morse_hw_irq_handle(mors);
	if (ret < 0)
		morse_err(mors, "IRQ handle failed: %d\n", ret);
}

static int morse_sdio_enable(struct morse_sdio *sdio)
{
	int ret;
	struct sdio_func *func = sdio->func;
	struct morse *mors = sdio_get_drvdata(func);

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret)
		morse_err(mors, "sdio_enable_func failed: %d\n", ret);
	sdio_release_host(func);
	return ret;
}

static void morse_sdio_release(struct morse_sdio *sdio)
{
	struct sdio_func *func = sdio->func;

	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

static int morse_sdio_enable_irq(struct morse_sdio *sdio)
{
	int ret;
	struct sdio_func *func = sdio->func;
	struct sdio_func *func1 = func->card->sdio_func[0];
	struct morse *mors = sdio_get_drvdata(func);

	sdio_claim_host(func);
	/* Register the isr */
	ret =  sdio_claim_irq(func1, morse_sdio_irq_handler);
	if (ret)
		morse_err(mors, "Failed to enable sdio irq: %d\n", ret);
	sdio_release_host(func);
	return ret;
}

static void morse_sdio_disable_irq(struct morse_sdio *sdio)
{
	struct sdio_func *func = sdio->func;
	struct sdio_func *func1 = func->card->sdio_func[0];

	sdio_claim_host(func);
	sdio_release_irq(func1);
	sdio_release_host(func);
}

static const struct of_device_id morse_of_match_table[] = {
	{ .compatible = "morse,mm6104", }, /* DEPRECATED */
	{ .compatible = "morse,mm610x", },
	{ },
};

static int morse_sdio_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	int ret = 0;
	uint32_t chip_id;
	struct morse *mors;
	struct morse_sdio *sdio;
	bool dl_fw = true, chk_fw = true;
	struct morse_hw_cfg *cfg = (struct morse_hw_cfg *)id->driver_data;

	dev_info(&func->dev,
		"sdio new func %d vendor 0x%x device 0x%x block 0x%x/0x%x\n",
		func->num, func->vendor, func->device, func->max_blksize,
		func->cur_blksize);

	if (is_thin_lmac_mode() && cfg == &mm6108c_cfg) {
		cfg = &mm6108c_tlm_cfg;
		dev_info(&func->dev,
			"Loading %s for Thin LMAC Mode\n",
			cfg->fw_name);
	} else if (is_virtual_sta_test_mode() && cfg == &mm6108c_cfg) {
		cfg = &mm6108c_vs_cfg;
		dev_info(&func->dev,
			"Loading %s for Virtual Station Test Mode\n",
			cfg->fw_name);
	}

	/* Consume func num 1 but dont do anything with it. */
	if (func->num == 1)
		return 0;

	/* Ignore anything but func 1 */
	if (func->num != 2)
		return -ENODEV;

	/* setting gpio pin configs from device tree */
	morse_of_probe(&func->dev, cfg, morse_of_match_table);

	mors = morse_mac_create(sizeof(*sdio), &func->dev);
	if (!mors) {
		dev_err(&func->dev, "morse_mac_create failed\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	sdio = (struct morse_sdio *)mors->drv_priv;
	sdio->func = func;
	sdio->id = id;
	sdio->enabled = true;
	sdio_set_drvdata(func, mors);
	sdio->dma_buffer = kzalloc(MORSE_DMA_BUFFER_SIZE, GFP_KERNEL);
	if (!sdio->dma_buffer) {
		morse_err(mors, "Failed to allocate DMA buffer\n");
		ret = -ENOMEM;
		goto err_dma;
	}

	mutex_init(&sdio->dma_buffer_mutex);

	ret = morse_sdio_enable(sdio);
	if (ret) {
		morse_err(mors, "morse_sdio_enable failed: %d\n", ret);
		goto err_cfg;
	}

	mors->cfg = cfg;
	mors->bus_ops = &morse_sdio_ops;
	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);
	morse_release_bus(mors);
	if  (ret < 0) {
		morse_err(mors, "morse read chip id failed: %d\n", ret);
		goto err_fw;
	}
	morse_info(mors, "CHIP ID: 0x%x", chip_id);
	mors->chip_id = chip_id;

	mors->board_serial = serial;
	morse_info(mors, "Board serial: %s", mors->board_serial);

	/* EFuse BXW check is done only for MM610x */
	if (enable_otp_check && !is_efuse_xtal_wait_supported(mors)) {
		morse_err(mors, "OTP check failed\n");
		ret = -EIO;
		goto err_cfg;
	}

	if (test_mode != MORSE_CONFIG_TEST_MODE_DISABLED)
		chk_fw = false;
	if (test_mode > MORSE_CONFIG_TEST_MODE_DOWNLOAD)
		dl_fw = false;
	ret = morse_firmware_init(mors, cfg->fw_name,
				dl_fw, chk_fw);
	if (ret) {
		morse_err(mors, "morse_firmware_init failed: %d\n", ret);
		goto err_fw;
	}

	morse_info(mors, "Firmware initialized : %s\n", cfg->fw_name);

	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		mors->chip_wq = create_singlethread_workqueue("MorseChipIfWorkQ");
		if (!mors->chip_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseChipIfWorkQ) failed\n");
			ret = -ENOMEM;
			goto err_fw;
		}
		mors->net_wq = create_singlethread_workqueue("MorseNetWorkQ");
		if (!mors->net_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseNetWorkQ) failed\n");
			ret = -ENOMEM;
			goto err_net_wq;
		}
		mors->command_wq =
			create_singlethread_workqueue("MorseCommandQ");
		if (!mors->command_wq) {
			morse_err(mors, "create_singlethread_workqueue(MorseCommandQ) failed\n");
			ret = -ENOMEM;
			goto err_command_wq;
		}

		ret = mors->cfg->ops->init(mors);
		if (ret) {
			morse_err(mors, "chip_if_init failed: %d\n", ret);
			goto err_buffs;
		}

		ret = morse_mac_register(mors);
		if (ret) {
			morse_err(mors, "morse_mac_register failed: %d\n", ret);
			goto err_mac;
		}
	}
	/* Now all set, enable SDIO interrupts */
	ret = morse_sdio_enable_irq(sdio);
	if (ret) {
		morse_err(mors, "morse_sdio_enable_irq failed: %d\n", ret);
		goto err_irq;
	}

#ifdef CONFIG_MORSE_ENABLE_TEST_MODES
	if (test_mode == MORSE_CONFIG_TEST_MODE_BUS)
		morse_bus_test(mors, "SDIO");
#endif

	/**
	 * Initialize PM runtime.
	 * Set the power.autosuspend_delay. If 'delay' is negative then runtime suspends are
	 * prevented. If power.use_autosuspend is set, pm_runtime_get_sync may be called which
	 * will put the device on pm_runtime_idle.
	 **/
	pm_runtime_set_autosuspend_delay(&func->dev, PM_RUNTIME_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&func->dev);
	pm_runtime_enable(&func->dev);
	pm_runtime_get_sync(&func->dev);

#ifdef CONFIG_MORSE_USER_ACCESS
	morse_uaccess = uaccess_alloc();
	if (IS_ERR(morse_uaccess)) {
		morse_pr_err("uaccess_alloc() failed\n");
		return PTR_ERR(morse_uaccess);
	}

	ret = uaccess_init(morse_uaccess);
	if (ret) {
		morse_pr_err("uaccess_init() failed: %d\n", ret);
		goto err_uaccess;
	}

	ret = uaccess_device_register(mors, morse_uaccess, &func->dev);
	if (ret) {
		morse_err(mors, "uaccess_device_register() failed: %d\n", ret);
		goto err_uaccess;
	}
#endif

	/* morse_buffers_test(mors); */
	return ret;

#ifdef CONFIG_MORSE_USER_ACCESS
err_uaccess:
	morse_sdio_disable_irq(sdio);
	uaccess_cleanup(morse_uaccess);
#endif
err_irq:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED)
		morse_mac_unregister(mors);
err_mac:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED)
		mors->cfg->ops->finish(mors);
err_buffs:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->command_wq);
		destroy_workqueue(mors->command_wq);
	}
err_command_wq:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->net_wq);
		destroy_workqueue(mors->net_wq);
	}
err_net_wq:
	if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
		flush_workqueue(mors->chip_wq);
		destroy_workqueue(mors->chip_wq);
	}
err_fw:
	morse_sdio_release(sdio);
err_cfg:
	kfree(sdio->dma_buffer);
err_dma:
	morse_mac_destroy(mors);
err_exit:
	pr_err("%s failed. The driver has not been loaded!\n", __func__);
	return ret;
}

static void morse_sdio_remove(struct sdio_func *func)
{
	struct morse *mors = sdio_get_drvdata(func);

	dev_info(&func->dev, "sdio removed func %d vendor 0x%x device 0x%x\n",
		 func->num, func->vendor, func->device);

	if (mors) {
		struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;


#ifdef CONFIG_MORSE_USER_ACCESS
		uaccess_device_unregister(mors);
		uaccess_cleanup(morse_uaccess);
#endif

		if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
			morse_mac_unregister(mors);
			morse_sdio_disable_irq(sdio);
			mors->cfg->ops->finish(mors);
			flush_workqueue(mors->chip_wq);
			destroy_workqueue(mors->chip_wq);
			flush_workqueue(mors->command_wq);
			destroy_workqueue(mors->command_wq);
			flush_workqueue(mors->net_wq);
			destroy_workqueue(mors->net_wq);
		}

		morse_sdio_release(sdio);
		kfree(sdio->dma_buffer);
		morse_mac_destroy(mors);

		/* Reset HW for a cleaner restart */
		sdio_set_drvdata(func, NULL);
		morse_sdio_reset(mors->cfg->mm_reset_gpio, func);
	}
}

static const struct sdio_device_id morse_sdio_devices[] = {
	/* MM610xA0 */
	{MORSE_SDIO_DEVICE(0x325B, MORSE_DEVICE_ID(0x6, 2, 0), mm6108c_cfg)},
	/* MM610xA1 */
	{MORSE_SDIO_DEVICE(0x325B, MORSE_DEVICE_ID(0x6, 3, 0), mm6108c_cfg)},
	{},
};

MODULE_DEVICE_TABLE(sdio, morse_sdio_devices);

static struct sdio_driver morse_sdio_driver = {
	.name = "morse_sdio",
	.id_table = morse_sdio_devices,
	.probe = morse_sdio_probe,
	.remove = morse_sdio_remove,
};

int __init morse_sdio_init(void)
{
	int ret;

	ret = sdio_register_driver(&morse_sdio_driver);
	if (ret)
		morse_pr_err("sdio_register_driver() failed: %d\n", ret);
	return ret;
}

void morse_sdio_set_irq(struct morse *mors, bool enable)
{
	struct morse_sdio *sdio = (struct morse_sdio *)mors->drv_priv;

	if (enable)
		morse_sdio_enable_irq(sdio);
	else
		morse_sdio_disable_irq(sdio);
}

void morse_sdio_exit(void)
{
	sdio_unregister_driver(&morse_sdio_driver);
}
