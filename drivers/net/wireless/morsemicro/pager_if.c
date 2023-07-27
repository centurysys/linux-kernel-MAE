/*
 * Copyright 2021 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "morse.h"
#include "bus.h"
#include "debug.h"
#include "skbq.h"
#include "hw.h"
#include "pager_if.h"
#include "skb_header.h"

static u32 enabled_irqs;

int morse_pager_irq_enable(const struct morse_pager *pager, bool enable)
{
	if (enable)
		enabled_irqs |= MORSE_PAGER_IRQ_MASK(pager->id);
	else
		enabled_irqs &= ~MORSE_PAGER_IRQ_MASK(pager->id);
	return morse_hw_irq_enable(pager->mors, pager->id, enable);
}

int morse_pager_irq_handler(struct morse *mors, uint32_t status)
{
	int count;
	struct morse_pager *pager;

	for (count = 0; count < mors->chip_if->pager_count; count++) {
		if (!((status & enabled_irqs) & MORSE_PAGER_IRQ_MASK(count)))
			continue;

		pager = &mors->chip_if->pagers[count];

		if (pager->flags & MORSE_PAGER_FLAGS_POPULATED)
			set_bit(MORSE_RX_PEND, &mors->chip_if->event_flags);
		else
			set_bit(MORSE_PAGE_RETURN_PEND, &mors->chip_if->event_flags);

		queue_work(mors->chip_wq, &mors->chip_if_work);
	}
	return 0;
}

void morse_pager_show(struct morse *mors, struct morse_pager *pager,
		      struct seq_file *file)
{
	seq_printf(file, "flags:0x%01x\n", pager->flags);
}

int morse_pager_init(struct morse *mors, struct morse_pager *pager,
		     int page_size, u8 flags, u8 id)
{
	pager->mors = mors;
	pager->flags = flags;
	pager->page_size_bytes = page_size;
	pager->parent = NULL;
	pager->id = id;

	return 0;
}

void morse_pager_finish(struct morse_pager *pager)
{

}
