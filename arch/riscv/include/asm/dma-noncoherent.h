/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#ifndef __ASM_RISCV_DMA_NON_COHERENT_H
#define __ASM_RISCV_DMA_NON_COHERENT_H

#ifdef CONFIG_RISCV_DMA_NONCOHERENT
struct riscv_dma_cache_sync {
	void (*cache_invalidate_dev)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
	void (*cache_clean_dev)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
	void (*cache_flush_dev)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
	void (*cache_invalidate_cpu)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
	void (*cache_clean_cpu)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
	void (*cache_flush_cpu)(phys_addr_t paddr, size_t size, enum dma_data_direction dir);
};

void riscv_dma_cache_sync_set(struct riscv_dma_cache_sync *ops);
#endif

#endif
