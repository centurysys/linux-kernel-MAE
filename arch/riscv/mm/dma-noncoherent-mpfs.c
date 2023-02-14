// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip PolarFire SoC specific functions to support DMA
 * when using non-coherent DMA access
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries
 */
#include <linux/cache.h>
#include <linux/dma-map-ops.h>
#include <asm/cacheflush.h>
#include <asm/dma-noncoherent.h>
#include <soc/sifive/sifive_l2_cache.h>

/*
 * Cache operations depending on function and direction argument.
 * to-device: write out cache to ddr
 * from-device: invalidate cache
 * birectional: write out cache to ddr and invalidate cache
 */
static void mpfs_sync_dma_for_device(phys_addr_t paddr, size_t size,
				     enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		sifive_l2_dma_cache_wback_inv(paddr, size);
		break;

	case DMA_FROM_DEVICE:
		sifive_l2_dma_cache_wback_inv(paddr, size);
		break;

	case DMA_BIDIRECTIONAL:
		sifive_l2_dma_cache_wback_inv(paddr, size);
		break;

	default:
		break;
	}
}

static void mpfs_cache_invalidate_dev(phys_addr_t paddr, size_t sz, enum dma_data_direction dir)
{
	mpfs_sync_dma_for_device(paddr, sz, dir);
}

static void mpfs_cache_clean_dev(phys_addr_t paddr, size_t sz, enum dma_data_direction dir)
{
	mpfs_sync_dma_for_device(paddr, sz, dir);
}

static void mpfs_cache_flush_dev(phys_addr_t paddr, size_t sz, enum dma_data_direction dir)
{
	mpfs_sync_dma_for_device(paddr, sz, dir);
}

static struct riscv_dma_cache_sync mpfs_dma_cache_sync_ops = {
	.cache_invalidate_dev = mpfs_cache_invalidate_dev,
	.cache_clean_dev = mpfs_cache_clean_dev,
	.cache_flush_dev = mpfs_cache_flush_dev,
};

static int __init mpfs_cache_register_ops(void)
{
	riscv_dma_cache_sync_set(&mpfs_dma_cache_sync_ops);

	return 0;
}
device_initcall(mpfs_cache_register_ops);
