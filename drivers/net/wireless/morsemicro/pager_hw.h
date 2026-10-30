/*
 * Copyright 2017-2025 Morse Micro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _MORSE_PAGER_HW_H_
#define _MORSE_PAGER_HW_H_

#include <linux/types.h>
#include <linux/workqueue.h>

#define MORSE_PAGER_FLAGS_DIR_TO_HOST	BIT(0)
#define MORSE_PAGER_FLAGS_DIR_TO_CHIP	BIT(1)
#define MORSE_PAGER_FLAGS_FREE		BIT(2)
#define MORSE_PAGER_FLAGS_POPULATED	BIT(3)

#define MORSE_PAGER_INT_STS(mors)	MORSE_REG_INT1_STS(mors)
#define MORSE_PAGER_INT_EN(mors)	MORSE_REG_INT1_EN(mors)
#define MORSE_PAGER_INT_SET(mors)	MORSE_REG_INT1_SET(mors)
#define MORSE_PAGER_INT_CLR(mors)	MORSE_REG_INT1_CLR(mors)

#define MORSE_PAGER_TRGR_STS(mors)	MORSE_REG_TRGR1_STS(mors)
#define MORSE_PAGER_TRGR_EN(mors)	MORSE_REG_TRGR1_EN(mors)
#define MORSE_PAGER_TRGR_SET(mors)	MORSE_REG_TRGR1_SET(mors)
#define MORSE_PAGER_TRGR_CLR(mors)	MORSE_REG_TRGR1_CLR(mors)

#define MORSE_PAGER_IRQ_MASK(ID)	(1 << (ID))

struct morse_pageset;
struct morse_page;
struct morse_skbq;
struct morse;

struct morse_pager_hw_entry {
	u8 flags;		/* Indicate direction of pager */
	u8 padding;
	__le16 page_size;	/* Page size in bytes */
	__le32 pop_addr;	/* Pager hardware instance pop address */
	__le32 push_addr;	/* Pager hardware instance push address */
} __packed;

struct morse_pager {
	u8 id;
	u8 flags;
	int page_size_bytes;
	struct morse *mors;
	struct morse_pageset *parent;
	struct work_struct work;
	void *aux_data;
};

int morse_pager_hw_page_write(struct morse_pager *pager,
			      struct morse_page *page,
			      int offset,
			      const char *buff,
			      int num_bytes);
int morse_pager_hw_page_read(struct morse_pager *pager,
			     struct morse_page *page,
			     int offset,
			     char *buff,
			     int num_bytes);
int morse_pager_hw_pop(struct morse_pager *pager, struct morse_page *page);
int morse_pager_hw_put(struct morse_pager *pager, struct morse_page *page);
int morse_pager_hw_notify(const struct morse_pager *pager);
void morse_pager_hw_flush_cache(struct morse_pager *pager);
int morse_pager_hw_pagers_init(struct morse *mors);
void morse_pager_hw_pagers_finish(struct morse *mors);

static inline const char *morse_pager_hw_name(u32 flags)
{
	if (flags & MORSE_PAGER_FLAGS_DIR_TO_HOST) {
		return (flags & MORSE_PAGER_FLAGS_FREE) ?
			"to_host (free)" : "to_host (populated)";
	} else if (flags & MORSE_PAGER_FLAGS_DIR_TO_CHIP) {
		return (flags & MORSE_PAGER_FLAGS_FREE) ?
			"from_host (free)" : "from_host (populated)";
	}

	return "unknown";
}

#endif /* !_MORSE_PAGER_HW_H_ */
