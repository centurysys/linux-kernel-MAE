/*
 * Copyright 2017-2022 Morse Micro
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/crc7.h>
#include <linux/mmc/sdio.h>		/* for SD_IO_XX commands */
#include <linux/crc-itu-t.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>

#include "morse.h"
#include "debug.h"
#include "bus.h"
#include "mac.h"
#include "firmware.h"
#include "of.h"
#include "crc16_xmodem.h"

#ifdef CONFIG_MORSE_USER_ACCESS
#include "uaccess.h"
#endif

struct morse_spi {
	bool enabled;
	u32	bulk_addr_base;
	u32 register_addr_base;
	struct spi_device *spi;

	/* Memory for command/response transfers */
	u8 *data;
	u8 *data_rx;

	/* for bulk data transfers */
	struct spi_transfer	t;
	struct spi_message	m;

	/* protects concurent access */
	struct mutex lock;

	/* for claim and release bus */
	struct mutex bus_lock;

	/* Num of bytes to insert between reads and writes, depending on frequency */
	u16 inter_block_delay_bytes;
	/* Maximum number of blks to write per SPI transaction */
	u8 max_block_count;
};

#ifdef CONFIG_MORSE_USER_ACCESS
struct uaccess *morse_spi_uaccess;
#endif

#define MORSE_SPI_DEVICE(dev, cfg) \
	.name = dev, \
	.driver_data = (kernel_ulong_t)&(cfg)

/*
 * SDIO Card Common Control Register Flags, per SDIO Specification Version 4.10, Part E1, Section 6.9.
 */

#define SDIO_CCCR_IEN_ADDR        0x04u
#define SDIO_CCCR_IEN_IENM        (1u)
#define SDIO_CCCR_IEN_IEN1        (1u << 1)

#define SDIO_CCCR_BIC_ADDR        0x07u
#define SDIO_CCCR_BIC_ECSI        (1u << 5)

/* Definitions for commands supported by Morse Chip
 * @SD_IO_MORSE_INIT: Morse Init with response
 * @SDIO_CMD0:  Reset
 */
#define SD_IO_RESET               0
#define SD_IO_MORSE_INIT          63

/*
 * Local protocol constants, internal to data block protocols.
 */

/* Response tokens used to ack each block written: */
#define SPI_MMC_RESPONSE_CODE(x)  ((x) & 0x1f)
#define SPI_RESPONSE_ACCEPTED     ((2 << 1)|1)
#define SPI_RESPONSE_CRC_ERR      ((5 << 1)|1)
#define SPI_RESPONSE_WRITE_ERR    ((6 << 1)|1)

#define SPI_TOKEN_SINGLE          0xfe /* single block r/w, multiblock read */
#define SPI_TOKEN_MULTI_WRITE     0xfc /* multiblock write */
#define SPI_TOKEN_COUNT           4    /* maximum number of bytes to search for block start */
#define SPI_R1_COUNT              4    /* maximum number of bytes to search for R1 response */
#define SPI_DATA_ACK_COUNT        4    /* maximum number of bytes to search for data block ACK */

#define SPI_SDIO_FUNC_0           0
#define SPI_SDIO_FUNC_1           1
#define SPI_SDIO_FUNC_2           2

#define MMC_SPI_BLOCKSIZE         512

#define MM610X_BUF_SIZE           (8 * 1024)

/* SW-5611:
 *
 * The value of SPI_MAX_TRANSACTION_SIZE was increased from 4096 to 8192
 * This will reduce the overhead of inter transaction delay to increase throughput
 *
 */
#define SPI_MAX_TRANSACTION_SIZE  8192        /* Maximum number of bytes per RPi SPI transaction */
#define SPI_MAX_TRANSFER_SIZE     (64 * 1024) /* Maximum number of bytes per SPI read/write */

#define SPI_CLK_SPEED					50000000   /* We need to set this value to get 50 MHz */
#define MAX_SPI_CLK_SPEED				50000000
#define SPI_CLK_PERIOD_NANO_S(clk_mhz)	(1000000000/clk_mhz)

#define SPI_DEFAULT_MAX_INTER_BLOCK_DELAY_BYTES	250


/* SPI clock speed */
static uint spi_clock_speed = SPI_CLK_SPEED;
module_param(spi_clock_speed, uint, 0644);
MODULE_PARM_DESC(spi_clock_speed, "SPI clock speed in Hz");

/* SPI bus edge IRQ compatability mode */
static bool spi_use_edge_irq;
module_param(spi_use_edge_irq, bool, 0644);
MODULE_PARM_DESC(spi_use_edge_irq, "Enable compatibility for edge IRQs on SPI");

static const struct spi_device_id mm610x_ids[] = {
	{ MORSE_SPI_DEVICE("mm6108", mm6108c_cfg) },
	{}
};
MODULE_DEVICE_TABLE(spi, mm610x_ids);

static const struct of_device_id morse_spi_of_match[] = {
	{ .compatible = "morse,mm610x-spi", (const void *)&mm6108c_cfg },
	{}
};
MODULE_DEVICE_TABLE(of, morse_spi_of_match);

static int morse_spi_setup(struct spi_device *spi, u32 max_speed_hz)
{
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = max_speed_hz;

	if (spi->max_speed_hz > SPI_CLK_SPEED) {
		dev_err(&spi->dev, "SPI clocks above 50MHz are not supported by Morse chip\n");
		return -EPERM;
	}

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_dbg(&spi->dev, "needs SPI mode %02x, %d KHz; %d\n",
				spi->mode, spi->max_speed_hz / 1000, ret);
		return ret;
	}

	return ret;
}

static int morse_spi_xfer(struct morse_spi *mspi, unsigned int len)
{
	int ret;

	if (len > SPI_MAX_TRANSACTION_SIZE) {
		WARN_ON(1);
		return -EIO;
	}

	mspi->t.len = len;
	ret = spi_sync_locked(mspi->spi, &mspi->m);

	return ret;
}

static void  __maybe_unused morse_spi_initsequence(struct morse_spi *mspi)
{
	struct spi_device *spi = mspi->spi;

	/*
	 * Do a burst with chipselect active-high.  We need to do this to
	 * meet the requirement of 74 clock cycles with both chipselect
	 * and CMD (MOSI) high before CMD0 ... after the card has been
	 * powered up to Vdd(min), and so is ready to take commands.
	 *
	 * Some cards are particularly needy of this (e.g. Viking "SD256")
	 * while most others don't seem to care.
	 *
	 * Note that this is one of the places MMC/SD plays games with the
	 * SPI protocol.  Another is that when chipselect is released while
	 * the card returns BUSY status, the clock must issue several cycles
	 * with chipselect high before the card will stop driving its output.
	 */
	spi->mode |= SPI_NO_CS;
	memset(mspi->data, 0xFF, 4 * 1024);
	morse_spi_xfer(mspi, 18);
	spi->mode &= ~SPI_NO_CS;
}

static void morse_spi_xfer_init(struct morse_spi *mspi)
{
	/* setup message from a single data buffer */
	spi_message_init(&mspi->m);

	mspi->m.is_dma_mapped = false;
	mspi->t.tx_buf = mspi->data;
	mspi->t.rx_buf = mspi->data_rx;
	mspi->t.cs_change = 0;

	spi_message_add_tail(&mspi->t, &mspi->m);
}

/* Search for R1 response */
static u8 *morse_spi_find_response(struct morse_spi *mspi, u8 *data, u8 *end)
{
	u8 *cp = data;

	while (cp < end && *cp == 0xff)
		cp++;

	/* Data block reads (R1 response types) may need more data... */
	if (cp == end)
		goto exit;

	if (*cp != 0x00)
		goto exit;

	/* point to next byte */
	cp++;

	/* Absorb an extra 0x00 if exist */
	if ((cp < end) &&  (*cp == 0x00))
		cp++;
	return cp;

exit:
	morse_pr_err("%s failed\n", __func__);
	return NULL;
}

static int morse_spi_cmd(struct morse_spi *mspi, u8 cmd, u32 arg)
{
	int ret = 0, len;
	u8 *cp = mspi->data;
	u8 *end;

	/*
	 * We can handle most commands (except block reads) in one full
	 * duplex I/O operation before either starting the next transfer
	 * (data block or command) or else deselecting the card.
	 *
	 * First, write 7 bytes:
	 *  - an all-ones byte to ensure the card is ready
	 *  - opcode byte (plus start and transmission bits)
	 *  - four bytes of big-endian argument
	 *  - crc7 (plus end bit) ... always computed, it's cheap
	 *
	 * We init the whole buffer to all-ones, which is what we need
	 * to write while we're reading (later) response data.
	 */

	memset(cp, 0xff, 20);
	/* clear response buffer */
	memset(mspi->data_rx, 0xff, 20);

	cp[1] = 0x40 | cmd;

	put_unaligned_be32(arg, cp + 2);

	cp[6] = crc7_be(0, cp + 1, 5) | 0x01;
	cp += 7;

	/* Allow 10 read attempts */
	end = cp + 13;
	len = end - mspi->data;

	morse_spi_xfer(mspi, len);

	/*
	 * Except for data block reads, the whole response will already
	 * be stored in the scratch buffer.  It's somewhere after the
	 * command and the first byte we read after it.  We ignore that
	 * first byte.  After STOP_TRANSMISSION command it may include
	 * two data bits, but otherwise it's all ones.
	 */
	if (!morse_spi_find_response(mspi, mspi->data_rx + 8, mspi->data_rx + len))
		/* Couldn't find successful R1 response */
		ret = -EPROTO;

	return ret;
}

static int morse_spi_cmd52(struct morse_spi *mspi, u8 fn, u8 data, u32 address)
{
	u32 arg = 0;
	u8 cmd = 0;
	u8 raw = 0;

	/**
	 * SDIO_CMD52 format as per PartE1_SDIO_Specification
	 * Start bit - 0
	 * Direction bit- 1
	 * Command Index (6bit) -SD_IO_RW_DIRECT
	 * rw bit - 0: read, 1: write
	 * Function (3 bits) - func 1 only supported now
	 * raw bit- read after write
	 * Stuff bit
	 * address - upto 17-bits
	 * Stuff bit
	 * data - upto 8-bits
	 * CRC- 7bit
	 * stop bit - Always 1
	 */
	cmd |= 0x40; /* Direction , 1 = towards device, 0 = towards host */
	cmd |= (SD_IO_RW_DIRECT & 0x3f);

	arg |= 0x1 << 31;
	arg |= (fn & 0x7) << 28;
	arg |= (raw & 0x1) << 27;
	arg |= (address & 0x1ffff) << 9;/* 17bit address */

	/* We always do CMD52 writes */
	arg |= (data & 0xff);

	return morse_spi_cmd(mspi, cmd, arg);
}

/* Search for block start token response */
static u8 *morse_spi_find_token(struct morse_spi *mspi, u8 *data, u8 *end)
{
	u8 *cp = data;

	while (cp < end && *cp == 0xff)
		cp++;

	if (cp == end)
		goto exit;

	if ((*cp != SPI_TOKEN_SINGLE) && (*cp != SPI_TOKEN_MULTI_WRITE))
		goto exit;

	/* point to the next byte */
	cp++;
	return cp;

exit:
	morse_pr_err("%s failed\n", __func__);
	return NULL;
}

/* Search for data block response */
static u8 *morse_spi_find_data_ack(struct morse_spi *mspi, u8 *data, u8 *end)
{
	u8 *cp = data;

	while (cp < end && *cp == 0xff)
		cp++;

	if (cp == end)
		goto exit;

	if (SPI_MMC_RESPONSE_CODE(*cp) != SPI_RESPONSE_ACCEPTED)
		goto exit;

	/* point to the next byte */
	cp++;
	return cp;

exit:
	morse_pr_err("%s failed\n", __func__);
	return NULL;
}

static int morse_spi_crc_verify(u8 *data, u32 data_size)
{
	/* crc be16 */
	u8 *crcp = data + data_size;
	u16 crc = ((u16)(*crcp) << 8) + ((u16)(*(crcp + 1)));

	/* SW-5611:
	 *
	 * Calculate the CRC 8 bytes at a time to minimize the overhead and increase throughput
	 *
	 */
	u16 crc_val = crc16xmodem_word(0, data, data_size);  // Calculate the CRC 8 bytes at a time to minimize the overhead and increase throughput

	if (crc == crc_val)
		return 0;

	morse_pr_err("%s failed expect 0x%04x found 0x%0x4\n", __func__, crc_val, crc);
	return -ECOMM;
}

static int morse_spi_put_cmd53(u8 fn, u32 address, u8 *data,
				u16 count, u8 write, bool block)
{
	u32 arg = 0;
	u8 cmd = 0;
	u8 opcode = 1;

	/* SDIO_CMD53 format as per PartE1_SDIO_Specification
	 * Start bit - 0
	 * Direction bit- 1
	 * Command Index(6bit) - SD_IO_RW_EXTENDED
	 * rw bit - 0: read, 1: write
	 * Function(3 bits) - func 1 only supported now
	 * Block mode bit - 0 is byte mode, 1 is block mode
	 * OP Code bit - 0 is fixed addr, 1 is incr addr
	 * address - upto 17-bits
	 * Byte/Blockcount - upto 9 bit
	 * CRC- 7bit
	 * stop bit - Always 1
	 */
	cmd |= 0x40;/* Direction , 1 = towards device, 0 = towards host */
	cmd |= (SD_IO_RW_EXTENDED & 0x3f);

	arg |= (write  & 0x1) << 31;
	arg |= (fn & 0x7) << 28;
	arg |= (opcode & 0x1) << 26;
	arg |= (address & 0x1ffff) << 9;/* 17bit address */
	arg |= (count & 0x1ff);
	arg |= (block & 0x1) << 27;

	memset(data, 0xff, 20);
	data[1] = 0x40 | cmd;
	put_unaligned_be32(arg, data+2);
	data[6] = crc7_be(0, data+1, 5) | 0x01;

	return 7;
}

/* if block flags is set, count is the number of blocks to send, else its the number of bytes */
static int morse_spi_cmd53_read(struct morse_spi *mspi, u8 fn, u32 address, u8 *data, u16 count, bool block)
{
	u8 *cp = mspi->data;
	u8 *resp;
	u8 *end;
	u32 data_size;
	/* Scale bytes delay to block */
	u32 extra_bytes = (count * mspi->inter_block_delay_bytes) / MMC_SPI_BLOCKSIZE;
	int i, len;

	memset(mspi->data, 0xFF, MM610X_BUF_SIZE);
	memset(mspi->data_rx, 0xFF, MM610X_BUF_SIZE);

	/* Insert command and argument */
	len = morse_spi_put_cmd53(fn, address, cp, count, 0, block);
	cp += len;

	resp = mspi->data_rx + len;

	/*
	 * Calculate number of clock cycles needed to get data.
	 * Transactions are either one block of few bytes (i.e less than
	 * MMC_SPI_BLOCKSIZE) or multiple block of MMC_SPI_BLOCKSIZE.
	 */

	/* Allow 4 bytes to get R1 response (usually comes in 2) */
	cp += 4;

	if (!block)
		/* Allow 4 for CRC and another 10 bytes for start block token & chip delays (usually comes in 2) */
		data_size = count + 4 + 4 + extra_bytes;
	else
		/* Each block need 512 bytes + Token + chip delays */
		data_size = count * (MMC_SPI_BLOCKSIZE + mspi->inter_block_delay_bytes + 2);

	memset(cp, 0xFF, data_size);
	cp += data_size;

	end = cp;
	len = cp - mspi->data;
	morse_spi_xfer(mspi, len);

	/*
	 * Response will already be stored in the data buffer.  It's
	 * somewhere after the command and the first byte we read after
	 * it. We ignore that first byte.
	 */

	/* Time to verify */
	cp = resp;
	end = cp + len;
	cp = morse_spi_find_response(mspi, cp, end);
	if (!cp)
		goto exit;

	data_size = block ? MMC_SPI_BLOCKSIZE : count;
	for (i = 0; i < (block ? count : 1); i++, data += data_size) {
		cp = morse_spi_find_token(mspi, cp, end);
		if (!cp)
			goto exit;

		if (morse_spi_crc_verify(cp, data_size))
			goto exit;

		memcpy(data, cp, data_size);
		cp += data_size + 4;
	}

	return count;

exit:
	morse_pr_err("%s failed\n", __func__);
	return -EPROTO;
}

static int morse_spi_cmd53_write(struct morse_spi *mspi, u8 fn, u32 address, u8 *data, u16 count, u8 block)
{
	u8 *cp = mspi->data;
	u8 *resp;
	u8 *end;
	u8 *ack = cp;
	u32 data_size;
	int i, len, ack_offset;

	memset(mspi->data, 0xFF, MM610X_BUF_SIZE);
	memset(mspi->data_rx, 0xFF, MM610X_BUF_SIZE);

	/* Insert command and argument */
	len = morse_spi_put_cmd53(fn, address, cp, count, 1, block);
	cp += len;

	/* Mark response point */
	resp = mspi->data_rx + len;

	/* Calculate number of clock cycles needed to get data.
	 * Transactions are either one block of few bytes (i.e less than
	 * MMC_SPI_BLOCKSIZE) or multiple blocks of MMC_SPI_BLOCKSIZE.
	 */
	/* Allow 4 bytes to get R1 response (usually comes in 2) */
	cp += 4;

	/* Allow 4 bytes to get 0xFF (i.e MISO ready) */
	cp += 4;

	data_size = block ? MMC_SPI_BLOCKSIZE : count;
	for (i = 0; i < (block ? count : 1); i++, data += MMC_SPI_BLOCKSIZE) {
		/* SW-5611:
		 *
		 * Calculate the CRC 8 bytes at a time to minimize the overhead and increase throughput
		 *
		 */
		u16 crc = crc16xmodem_word(0, data, data_size);

		/* WR: ACK should be set below (after sending the block). However for
		 * seems the chip is providing the ACKs (some times) a bit too early.
		 * For this we start searching for an ACK once we start sendind data.
		 */
		/* Mark data ack point */
		if (i == 0)
			ack = cp;

		/* tx token */
		*cp = block ? SPI_TOKEN_MULTI_WRITE : SPI_TOKEN_SINGLE;
		cp++;

		/* data */
		memcpy(cp, data, data_size);
		cp += data_size;

		/* crc */
		*cp = (crc & 0xFF00) >> 8;
		*(cp + 1) = (crc & 0xFF);
		cp += sizeof(crc);

		/* Allow more bytes for status and chip processing (depends on CLK)*/
		cp += block ? mspi->inter_block_delay_bytes : 4;
	}

	/* Do the actual transfer */
	end = cp;
	len = end - mspi->data;
	morse_spi_xfer(mspi, len);

	/* Time to verify */
	cp = resp;
	end = mspi->data_rx + len;
	cp = morse_spi_find_response(mspi, cp, end);
	if (!cp)
		goto exit;

	ack_offset = ack - mspi->data;
	ack = mspi->data_rx + ack_offset;

	/* SW-5611:
	 *
	 * If in block mode, start searching for the data ack exactly where it is expected.
	 * This will improve the throughput. For 14 * 512 Bytes of data transfer, the time
	 * it takes to find the response is reduced from 33 uS to 1 uS
	 *
	 */
	ack += block ? (1 /* TOKEN */ + MMC_SPI_BLOCKSIZE /* data size */ + 2 /* crc */) : 0;
	data_size = 1 /* TOKEN */ + MMC_SPI_BLOCKSIZE + 2 /* crc */ + mspi->inter_block_delay_bytes;
	for (i = 0; i < (block ? count : 1); i++, ack += data_size) {
		cp = ack;
		cp = morse_spi_find_data_ack(mspi, cp, end);
		if (!cp)
			goto exit;
	}

	return count;

exit:
	morse_pr_err("%s failed\n", __func__);
	return -EPROTO;
}

static u32 morse_spi_calculate_base_address(u32 address, u8 access)
{
	return (address & MORSE_SDIO_RW_ADDR_BOUNDARY_MASK) | (access & 0x3);
}

static int morse_spi_set_func_address_base(struct morse_spi *mspi, u32 address, u8 access, bool bulk)
{
	int	ret = 0;
	u8	base[4];
	u32	calculated_addr_base = morse_spi_calculate_base_address(address, access);
	u32	*current_addr_base = bulk ? &mspi->bulk_addr_base : &mspi->register_addr_base;
	u8 func_to_use = bulk ? SPI_SDIO_FUNC_2 : SPI_SDIO_FUNC_1;
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);

	if ((*current_addr_base) == calculated_addr_base)
		return ret;

	base[0] = (u8)((address & 0x00FF0000) >> 16);
	base[1] = (u8)((address & 0xFF000000) >> 24);
	base[2] = access & 0x3; /* 1, 2 or 4 byte access */

	/* Write them as single bytes for now */
	if (base[0] != (u8)(((*current_addr_base) & 0x00FF0000) >> 16)) {
		ret = morse_spi_cmd52(mspi, func_to_use, base[0], MORSE_REG_ADDRESS_WINDOW_0);
		if (ret)
			goto err;
	}

	if (base[1] != (u8)(((*current_addr_base) & 0xFF000000) >> 24)) {
		ret = morse_spi_cmd52(mspi, func_to_use, base[1], MORSE_REG_ADDRESS_WINDOW_1);
		if (ret)
			goto err;
	}

	if (base[2] != (u8)(((*current_addr_base) & 0x3))) {
		ret = morse_spi_cmd52(mspi, func_to_use, base[2], MORSE_REG_ADDRESS_CONFIG);
		if (ret)
			goto err;
	}

	*current_addr_base = calculated_addr_base;
	return ret;

err:
	morse_err(mors, "%s failed (errno=%d)\n", __func__, ret);
	return ret;
}

static int morse_spi_get_func(struct morse_spi *mspi,
					u32 address, ssize_t size, u8 access)
{
	int	ret = 0;
	int	func_to_use;
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);
	u32 calculated_base_address = morse_spi_calculate_base_address(address, access);

	if (size > sizeof(u32)) {
		ret = morse_spi_set_func_address_base(mspi, address, access, true);
		MORSE_WARN_ON(mspi->bulk_addr_base == 0);
		func_to_use = SPI_SDIO_FUNC_2;
	} else if (mspi->bulk_addr_base == calculated_base_address)
		func_to_use = SPI_SDIO_FUNC_2;
	else {
		ret = morse_spi_set_func_address_base(mspi, address, access, false);
		MORSE_WARN_ON(mspi->register_addr_base == 0);
		func_to_use = SPI_SDIO_FUNC_1;
	}

	if (ret)
		morse_err(mors, "%s failed\n", __func__);

	return (!ret) ? func_to_use : ret;
}

static void morse_spi_reset_base_address(struct morse_spi *mspi)
{
	mspi->bulk_addr_base = 0;
	mspi->register_addr_base = 0;
}

static int morse_spi_mem_read(struct morse_spi *mspi, u32 address, u8 *data, u32 size)
{
	int ret = 0;
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);
	u32 bytes = size & (MMC_SPI_BLOCKSIZE - 1);
	u32 blks = (size - bytes) / MMC_SPI_BLOCKSIZE;
	u32 blks_done = 0;
	int access = (size & 0x3) ?
			MORSE_CONFIG_ACCESS_1BYTE : MORSE_CONFIG_ACCESS_4BYTE;
	int func_to_use;

	mutex_lock(&mspi->lock);

	func_to_use = morse_spi_get_func(mspi, address, size, access);
	if ((func_to_use != SPI_SDIO_FUNC_1) &&
		(func_to_use != SPI_SDIO_FUNC_2))
		goto exit;

	address &= 0xFFFF; /* remove base and keep offset */
	if (blks) {
		/* we only have 4K per SPI transaction */
		while (blks - blks_done) {
			int blk_count = min_t(int, mspi->max_block_count, blks - blks_done);

			ret = morse_spi_cmd53_read(mspi, func_to_use,
						   address + blks_done * MMC_SPI_BLOCKSIZE,
						   data + blks_done * MMC_SPI_BLOCKSIZE,
						   blk_count, 1);
			if (ret < 0)
				goto exit;

			blks_done += blk_count;
		}
	}

	if (bytes) {
		ret = morse_spi_cmd53_read(mspi, func_to_use, address + blks_done * MMC_SPI_BLOCKSIZE,
					   data + blks_done * MMC_SPI_BLOCKSIZE, bytes, 0);
		if (ret < 0)
			goto exit;
	}
	/* SW-3875: seems like sdio/spi read can sometimes go wrong and read first 4-bytes word twice,
	 * overwriting second word (hence, tail will be overwritten with 'sync' byte). It seems
	 * like reading those again will fetch the correct word. Let's do that.
	 * NB: if been corrupted again, pass it anyway and upper layers will handle it
	 */
	if (access == MORSE_CONFIG_ACCESS_4BYTE) {
		if (*(u32 *)data && !memcmp(data, data+4, 4)) {
			/* morse_spi_cmd53_read corrupts second word. Lets try one more time before passing up */
			morse_spi_cmd53_read(mspi, func_to_use, address, data, 8, 0);
		}
	}

	mutex_unlock(&mspi->lock);
	return size;

exit:
	mutex_unlock(&mspi->lock);
	morse_err(mors, "%s failed (errno=%d)\n", __func__, ret);
	return ret;
}

static int morse_spi_mem_write(struct morse_spi *mspi, u32 address,
								u8 *data, u32 size)
{
	int ret = 0;
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);
	u32 bytes = size & (MMC_SPI_BLOCKSIZE - 1);
	u32 blks = (size - bytes) / MMC_SPI_BLOCKSIZE;
	u32 blks_done = 0;
	int access = (size & 0x3) ?
			MORSE_CONFIG_ACCESS_1BYTE : MORSE_CONFIG_ACCESS_4BYTE;
	int func_to_use;

	mutex_lock(&mspi->lock);

	func_to_use = morse_spi_get_func(mspi, address, size, access);
	if ((func_to_use != SPI_SDIO_FUNC_1) &&
		(func_to_use != SPI_SDIO_FUNC_2))
		goto exit;

	address &= 0xFFFF; /* remove base and keep offset */
	if (blks) {
		/* we only have 4K per SPI transaction */
		while (blks - blks_done) {
			int blk_count = min_t(int, mspi->max_block_count, blks - blks_done);

			ret = morse_spi_cmd53_write(mspi, func_to_use,
						    address + blks_done * MMC_SPI_BLOCKSIZE,
						    data + blks_done * MMC_SPI_BLOCKSIZE,
						    blk_count, 1);
			if (ret < 0)
				goto exit;

			blks_done += blk_count;
		}
	}

	if (bytes) {
		ret = morse_spi_cmd53_write(mspi, func_to_use, address + blks_done * MMC_SPI_BLOCKSIZE,
					    data + blks_done * MMC_SPI_BLOCKSIZE, bytes, 0);
		if (ret < 0)
			goto exit;
	}

	mutex_unlock(&mspi->lock);
	return size;

exit:
	mutex_unlock(&mspi->lock);
	morse_err(mors, "%s failed (errno=%d)\n", __func__, ret);
	return ret;
}

static int morse_spi_dm_write(struct morse *mors, u32 address,
					const u8 *data, u32 len)
{
	ssize_t offset = 0, ret;
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

	while (offset < len) {
		ret = morse_spi_mem_write(mspi, address + offset,
					   (u8 *)(data + offset),
					   min((ssize_t)(len - offset), (ssize_t)SPI_MAX_TRANSFER_SIZE)); /* cast to ssize_t on both sides so the build on x64 doesn't complain. */
		if (ret < 0) {
			morse_err(mors, "%s failed (errno=%d)\n", __func__, (int)ret);
			return -EIO;
		}
		offset += ret;
	}

	return 0;
}

static int morse_spi_dm_read(struct morse *mors, u32 address,
						  u8 *data, u32 len)
{
	ssize_t offset = 0, ret;
	struct morse_spi *spi = (struct morse_spi *)mors->drv_priv;

	while (offset < len) {
		ret = morse_spi_mem_read(spi, address + offset,
					  (u8 *)(data + offset),
					  min((ssize_t)(len - offset), (ssize_t)SPI_MAX_TRANSFER_SIZE)); /* cast to ssize_t on both sides so the build on x64 doesn't complain. */
		if (ret < 0) {
			morse_err(mors, "%s failed (errno=%d)\n", __func__, (int)ret);
			return -EIO;
		}
		offset += ret;
	}

	return 0;
}

static int morse_spi_reg32_write(struct morse *mors, u32 address, u32 value)
{
	int ret = 0;
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

	ret = morse_spi_mem_write(mspi, address, (u8 *)&value, sizeof(value));

	/* Reset base address after software reset */
	if (address == MORSE_REG_RESET(mors) &&
		value == MORSE_REG_RESET_VALUE(mors)) {
		morse_dbg(mors, "SPI reset detected, invalidating base addr\n");
		morse_spi_reset_base_address(mspi);
	}

	if (ret == sizeof(value))
		return 0;

	morse_err(mors, "%s failed (errno=%d)\n", __func__, (int)ret);
	return -EIO;
}

static int morse_spi_reg32_read(struct morse *mors, u32 address, u32 *value)
{
	int ret = 0;
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

	ret = morse_spi_mem_read(mspi, address, (u8 *)value, sizeof(*value));

	if (ret == sizeof(*value))
		return 0;

	morse_err(mors, "%s failed (errno=%d)\n", __func__, (int)ret);
	return -EIO;
}

static irqreturn_t morse_spi_irq_handler(int irq, struct morse_spi *mspi)
{
	int ret = 0;
	struct morse *mors = spi_get_drvdata(mspi->spi);

	MORSE_WARN_ON(mors == NULL);
	if (irq == gpio_to_irq(mors->cfg->mm_spi_irq_gpio)) {

		/*
		 * If we are using edge interrupts, we need to continuously service the IRQ until
		 * either the chip has cleared all its IRQ bits, or the pin goes high again.
		 */
		do {
			ret = morse_hw_irq_handle(mors);
		} while (spi_use_edge_irq && ret && !gpio_get_value(mors->cfg->mm_spi_irq_gpio));

		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static void morse_spi_enable_irq(struct morse_spi *mspi)
{
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);

	if (spi->irq == gpio_to_irq(mors->cfg->mm_spi_irq_gpio))
		enable_irq(spi->irq);
}

static void morse_spi_disable_irq(struct morse_spi *mspi)
{
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);

	if (spi->irq == gpio_to_irq(mors->cfg->mm_spi_irq_gpio))
		disable_irq(spi->irq);
}

static int morse_spi_setup_irq(struct morse_spi *mspi)
{
	int ret;
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);

	/* Register GPIO IRQ */
	ret = gpio_request(mors->cfg->mm_spi_irq_gpio, "mm610x_spi_irq_gpio");
	if (ret < 0) {
		morse_pr_err("Failed to acquire spi irq gpio.\n");
		return ret;
	}

	/* Setup pull-down */
	gpio_direction_input(mors->cfg->mm_spi_irq_gpio);
	spi->irq = gpio_to_irq(mors->cfg->mm_spi_irq_gpio);

	/* Enable interrupts from Chip */
	ret = morse_spi_cmd52(mspi, SPI_SDIO_FUNC_0,
						SDIO_CCCR_IEN_IENM | SDIO_CCCR_IEN_IEN1,
						SDIO_CCCR_IEN_ADDR);
	if (!ret)
		ret = morse_spi_cmd52(mspi, SPI_SDIO_FUNC_0,
							SDIO_CCCR_BIC_ECSI, SDIO_CCCR_BIC_ADDR);

	if (!ret)
		ret = request_threaded_irq(
			spi->irq, NULL,
			(irq_handler_t) morse_spi_irq_handler,
			(spi_use_edge_irq ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_LOW) | IRQF_ONESHOT,
			"Morse SPI IRQ",
			mspi);

	return ret;
}

static void morse_spi_remove_irq(struct morse_spi *mspi)
{
	struct spi_device *spi = mspi->spi;
	struct morse *mors = spi_get_drvdata(spi);

	free_irq(spi->irq, mspi);
	gpio_free(mors->cfg->mm_spi_irq_gpio);
}

void morse_spi_set_irq(struct morse *mors, bool enable)
{
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

	if (enable)
		morse_spi_enable_irq(mspi);
	else
		morse_spi_disable_irq(mspi);
}

static void morse_spi_reset(int reset_pin, struct spi_device *spi)
{
	morse_hw_reset(reset_pin);
}

static int morse_spi_remove(struct spi_device *spi)
{
	int ret = 0;
	struct morse *mors;

	mors = spi_get_drvdata(spi);
	if (mors) {
		struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

		if (test_mode == MORSE_CONFIG_TEST_MODE_DISABLED) {
			morse_mac_unregister(mors);
			morse_spi_disable_irq(mspi);
			flush_workqueue(mors->chip_wq);
			destroy_workqueue(mors->chip_wq);
			flush_workqueue(mors->command_wq);
			destroy_workqueue(mors->command_wq);
			flush_workqueue(mors->net_wq);
			destroy_workqueue(mors->net_wq);
			mors->cfg->ops->finish(mors);
		}

		morse_spi_remove_irq(mspi);
		kfree(mspi->data_rx);
		kfree(mspi->data);
#ifdef CONFIG_MORSE_USER_ACCESS
		uaccess_device_unregister(mors);
		uaccess_cleanup(morse_spi_uaccess);
#endif
		morse_mac_destroy(mors);
		dev_set_drvdata(&spi->dev, NULL);
	}

	dev_info(&spi->dev, "Morse SPI device removed\n");
	morse_spi_reset(mors->cfg->mm_reset_gpio, spi);
	return ret;
}

void morse_spi_claim_bus(struct morse *mors)
{
	struct morse_spi *mspi;

	mspi = (struct morse_spi *)mors->drv_priv;
	mutex_lock(&mspi->bus_lock);
}

void morse_spi_release_bus(struct morse *mors)
{
	struct morse_spi *mspi;

	mspi = (struct morse_spi *)mors->drv_priv;
	mutex_unlock(&mspi->bus_lock);
}

static int morse_spi_bus_reset(struct morse *mors)
{
	int ret = 0;
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;
	struct spi_device *spi = mspi->spi;

	morse_spi_remove(spi);

	return ret;
}

static void morse_spi_bus_enable(struct morse *mors, bool enable)
{
	struct morse_spi *mspi = (struct morse_spi *)mors->drv_priv;

	if (enable) {
		mspi->enabled = true;
		mors->bus_ops->set_irq(mors, 1);
		morse_dbg(mors, "%s: enabling bus\n", __func__);
	} else {
		mors->bus_ops->set_irq(mors, 0);
		morse_spi_reset_base_address(mspi);
		mspi->enabled = false;
		morse_dbg(mors, "%s: disabling bus\n", __func__);
	}
}

static const struct morse_bus_ops morse_spi_ops = {
	.dm_read = morse_spi_dm_read,
	.dm_write = morse_spi_dm_write,
	.reg32_read = morse_spi_reg32_read,
	.reg32_write = morse_spi_reg32_write,
	.set_bus_enable = morse_spi_bus_enable,
	.claim = morse_spi_claim_bus,
	.release = morse_spi_release_bus,
	.reset = morse_spi_bus_reset,
	.set_irq = morse_spi_set_irq,
};

static int morse_spi_probe(struct spi_device *spi)
{
	int i, ret = 0;
	bool dl_fw = true, chk_fw = true;
	struct morse *mors;
	struct morse_spi *mspi;
	const struct of_device_id *match;
	struct morse_hw_cfg *cfg;
	int32_t inter_block_delay_nano_s;

	match = of_match_device(of_match_ptr(morse_spi_of_match), &spi->dev);
	if (match)
		cfg = (struct morse_hw_cfg *)match->data;
	else
		cfg = (struct morse_hw_cfg *)spi_get_device_id(spi)->driver_data;

	ret = morse_spi_setup(spi, spi_clock_speed);
	if (ret < 0) {
		pr_err("morse_spi_setup failed\n");
		goto err_exit;
	}

	/* setting gpio pin configs from device tree */
	morse_of_probe(&spi->dev, cfg, morse_spi_of_match);

	mors = morse_mac_create(sizeof(*mspi), &spi->dev);
	if (!mors) {
		dev_err(&spi->dev, "morse_mac_create failed\n");
		return -ENOMEM;
	}

	/* update chip configuration */
	mors->cfg = cfg;
	mors->bus_ops = &morse_spi_ops;

	/* preallocate dma buffers */
	mspi = (struct morse_spi *)mors->drv_priv;
	mspi->data = kmalloc(MM610X_BUF_SIZE, GFP_KERNEL);
	mspi->data_rx = kmalloc(MM610X_BUF_SIZE, GFP_KERNEL);
	if (!mspi->data) {
		morse_err(mors, "%s Failed to allocate DMA buffers (size=%d bytes)\n",
				__func__, MM610X_BUF_SIZE);
		ret = -ENOMEM;
		goto err_nobuf;
	}

	mspi->spi = spi;

	morse_spi_reset_base_address(mspi);

	mspi->inter_block_delay_bytes = SPI_DEFAULT_MAX_INTER_BLOCK_DELAY_BYTES; /* let's first assign the default value, before enabling burst mode */
	mspi->max_block_count = SPI_MAX_TRANSACTION_SIZE / (MMC_SPI_BLOCKSIZE + mspi->inter_block_delay_bytes);

	mutex_init(&mspi->lock);
	mutex_init(&mspi->bus_lock);
	spi_set_drvdata(spi, mors);

	/* spi init */
	morse_spi_xfer_init(mspi);
	morse_spi_initsequence(mspi);

	/*
	 * Give enough time for chip to init, Max 3 attempts to init the chip.
	 * Morse chip requires few bytes to be written after CMD63 to get it
	 * to active state. DO NOT CHANGE THIS INIT
	 */
	for (i = 0; i < 3; i++) {
		/* init sequence for morse chip argument is 32 bit 0s */
		ret = morse_spi_cmd(mspi, SD_IO_MORSE_INIT, 0x00000000);
		if (!ret)
			break;
		morse_spi_cmd(mspi, SD_IO_RESET, 0x00000000);
	}

	if (ret)
		goto err_cfg;

	ret = morse_spi_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &mors->chip_id);
	if (!ret) {
		/* Find out if the chip id matches our records */
		if (!morse_hw_is_valid_chip_id(mors->chip_id, mors->cfg->valid_chip_ids)) {
			morse_err(mors, "%s Morse chip (ChipId=0x%x) not supported\n",
					__func__, mors->chip_id);
			goto err_cfg;
		}
		mors->board_serial = serial;

		/*
		 * Now that a valid chip id has been found, let's enable burst mode.
		 * The function below will check if burst mode is supported and if so, it will enable it.
		 * A NULL check is also performed to make sure the chips that don't have this will work with the default inter block delay
		 */
		if (mors->cfg->enable_sdio_burst_mode != NULL) {
			inter_block_delay_nano_s = mors->cfg->enable_sdio_burst_mode(mors);

			if (inter_block_delay_nano_s > 0) {
				/* No Errors detected, therefore, the value returned can be used to set the inter block delay*/
				mspi->inter_block_delay_bytes = inter_block_delay_nano_s / (SPI_CLK_PERIOD_NANO_S(MAX_SPI_CLK_SPEED) * 8);
				mspi->max_block_count = SPI_MAX_TRANSACTION_SIZE / (MMC_SPI_BLOCKSIZE + mspi->inter_block_delay_bytes);
			}
		}
	} else
		goto err_cfg;

	morse_info(mors, "New Morse SPI device found, ChipId=0x%x, SerialNumber=%s\n",
				mors->chip_id, mors->board_serial);

	morse_info(mors, "New Morse SPI device setting: Clock=%d(MHz), BlockDelay=%d(bytes), BlockCountPerTransaction=%d(blocks)\n",
				MAX_SPI_CLK_SPEED / 1000000, mspi->inter_block_delay_bytes, mspi->max_block_count);

	/* EFuse BXW check is done only for MM610x */
	if (enable_otp_check && !is_efuse_xtal_wait_supported(mors)) {
		morse_err(mors, "OTP check failed\n");
		ret = -EIO;
		goto err_cfg;
	}

#ifdef CONFIG_MORSE_USER_ACCESS
	morse_spi_uaccess = uaccess_alloc();
	if (IS_ERR(morse_spi_uaccess)) {
		morse_pr_err("uaccess_alloc() failed\n");
		return PTR_ERR(morse_spi_uaccess);
	}

	ret = uaccess_init(morse_spi_uaccess);
	if (ret) {
		morse_pr_err("uaccess_init() failed: %d\n", ret);
		goto err_uaccess;
	}

	ret = uaccess_device_register(mors, morse_spi_uaccess, &spi->dev);
	if (ret) {
		morse_err(mors, "uaccess_device_register() failed: %d\n", ret);
		goto err_uaccess;
	}
#endif

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
	/* Now all set, enable SPI interrupts */
	ret = morse_spi_setup_irq(mspi);
	if (ret) {
		morse_err(mors, "morse_spi_setup_irq() failed: %d\n", ret);
		goto err_irq;
	}

#ifdef CONFIG_MORSE_ENABLE_TEST_MODES
	if (test_mode == MORSE_CONFIG_TEST_MODE_BUS)
		morse_bus_test(mors, "SPI");
#endif

	return ret;

err_irq:
	morse_spi_remove_irq(mspi);
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
#ifdef CONFIG_MORSE_USER_ACCESS
err_uaccess:
	morse_spi_disable_irq(mspi);
	uaccess_cleanup(morse_spi_uaccess);
#endif
err_cfg:
	kfree(mspi->data_rx);
	kfree(mspi->data);
err_nobuf:
	morse_mac_destroy(mors);

err_exit:
	pr_err("%s failed. The driver has not been loaded!\n", __func__);

	return ret;
}

/* Register as SPI protocol driver */
static struct spi_driver morse_spi_driver = {
	.driver = {
		.name = "morse_spi",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(morse_spi_of_match),
	},
	.id_table = mm610x_ids,
	.probe = morse_spi_probe,
	.remove = morse_spi_remove,
};

int __init morse_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&morse_spi_driver);

	if (ret)
		morse_pr_err("spi_register_driver() failed (errno=%d)\n", ret);
	return ret;
}

void __exit morse_spi_exit(void)
{
	spi_unregister_driver(&morse_spi_driver);
}
