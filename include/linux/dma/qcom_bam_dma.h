/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _QCOM_BAM_DMA_H
#define _QCOM_BAM_DMA_H

#include <linux/dma-mapping.h>

#define DESC_FLAG_INT BIT(15)
#define DESC_FLAG_EOT BIT(14)
#define DESC_FLAG_EOB BIT(13)
#define DESC_FLAG_NWD BIT(12)
#define DESC_FLAG_CMD BIT(11)

/*
 * QCOM BAM DMA SGL struct
 *
 * @sgl: DMA SGL
 * @dma_flags: BAM DMA flags
 */
struct qcom_bam_sgl {
	struct scatterlist sgl;
	unsigned int dma_flags;
};

/*
 * qcom_bam_sg_init_table - Init QCOM BAM SGL
 * @bam_sgl: bam sgl
 * @nents: number of entries in bam sgl
 *
 * This function performs the initialization for each SGL in BAM SGL
 * with generic SGL API.
 */
static inline void qcom_bam_sg_init_table(struct qcom_bam_sgl *bam_sgl,
		unsigned int nents)
{
	int i;

	for (i = 0; i < nents; i++)
		sg_init_table(&bam_sgl[i].sgl, 1);
}

/*
 * qcom_bam_unmap_sg - Unmap QCOM BAM SGL
 * @dev: device for which unmapping needs to be done
 * @bam_sgl: bam sgl
 * @nents: number of entries in bam sgl
 * @dir: dma transfer direction
 *
 * This function performs the DMA unmapping for each SGL in BAM SGL
 * with generic SGL API.
 */
static inline void qcom_bam_unmap_sg(struct device *dev,
	struct qcom_bam_sgl *bam_sgl, int nents, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++)
		dma_unmap_sg(dev, &bam_sgl[i].sgl, 1, dir);
}

/*
 * qcom_bam_map_sg - Map QCOM BAM SGL
 * @dev: device for which mapping needs to be done
 * @bam_sgl: bam sgl
 * @nents: number of entries in bam sgl
 * @dir: dma transfer direction
 *
 * This function performs the DMA mapping for each SGL in BAM SGL
 * with generic SGL API.
 *
 * returns 0 on error and > 0 on success
 */
static inline int qcom_bam_map_sg(struct device *dev,
	struct qcom_bam_sgl *bam_sgl, int nents, enum dma_data_direction dir)
{
	int i, ret = 0;

	for (i = 0; i < nents; i++) {
		ret = dma_map_sg(dev, &bam_sgl[i].sgl, 1, dir);
		if (!ret)
			break;
	}

	/* unmap the mapped sgl from previous loop in case of error */
	if (!ret)
		qcom_bam_unmap_sg(dev, bam_sgl, i, dir);

	return ret;
}
#endif
