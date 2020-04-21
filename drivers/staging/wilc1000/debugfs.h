/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#ifndef WILC_DEBUGFS_H
#define WILC_DEBUGFS_H

#include <linux/kern_levels.h>
#include <linux/version.h>
#if KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE
#include <linux/of_irq.h>
#endif

#define GENERIC_DBG		BIT(0)
#define HOSTAPD_DBG		BIT(1)
#define HOSTINF_DBG		BIT(2)
#define CORECONFIG_DBG		BIT(3)
#define CFG80211_DBG		BIT(4)
#define INT_DBG			BIT(5)
#define TX_DBG			BIT(6)
#define RX_DBG			BIT(7)
#define TCP_ENH			BIT(8)
#define INIT_DBG		BIT(9)
#define PWRDEV_DBG		BIT(10)
#define DBG_REGION_ALL		(BIT(11)-1)

extern atomic_t WILC_DEBUG_REGION;

#define PRINT_D(netdev, region, format, ...) do { \
	if (atomic_read(&WILC_DEBUG_REGION)&(region))\
		netdev_dbg(netdev, "DBG [%s: %d] "format, __func__, __LINE__,\
		   ##__VA_ARGS__); } \
	while (0)

#define PRINT_INFO(netdev, region, format, ...) do { \
	if (atomic_read(&WILC_DEBUG_REGION)&(region))\
		netdev_info(netdev, "INFO [%s]"format, __func__, \
		##__VA_ARGS__); } \
	while (0)

#define PRINT_WRN(netdev, region, format, ...) do { \
	if (atomic_read(&WILC_DEBUG_REGION)&(region))\
		netdev_warn(netdev, "WRN [%s: %d]"format, __func__, __LINE__,\
		    ##__VA_ARGS__); } \
	while (0)

#define PRINT_ER(netdev, format, ...) netdev_err(netdev, "ERR [%s:%d] "format,\
	__func__, __LINE__, ##__VA_ARGS__)

int wilc_debugfs_init(void);
void wilc_debugfs_remove(void);

#if KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE
/**
 * of_irq_parse_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @addr:	address specifier (start of "reg" property of the device)
 *              in be32 format
 * @out_irq:	structure of_irq updated by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent. It takes an interrupt specifier structure as
 * input, walks the tree looking for any interrupt-map properties, translates
 * the specifier for each map, and then returns the translated map.
 */
int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	const __be32 *tmp, *imap, *imask;
	const __be32 dummy_imask[] = { [0 ... MAX_PHANDLE_ARGS] = ~0 };
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

#ifdef DEBUG
	of_print_phandle_args("of_irq_parse_raw: ", out_irq);
#endif

	ipar = of_node_get(out_irq->np);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = be32_to_cpu(*tmp);
			break;
		}
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

	pr_debug("of_irq_parse_raw: ipar=%s, size=%d\n",
		 of_node_full_name(ipar), intsize);

	if (out_irq->args_count != intsize)
		return -EINVAL;

	/* Look for this #address-cells. We have to implement the old linux
	 * trick of looking for the parent here as some device-trees rely on it
	 */
	old = of_node_get(ipar);
	do {
		tmp = of_get_property(old, "#address-cells", NULL);
		tnode = of_get_parent(old);
		of_node_put(old);
		old = tnode;
	} while (old && tmp == NULL);
	of_node_put(old);
	old = NULL;
	addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

	pr_debug(" -> addrsize=%d\n", addrsize);

	/* Range check so that the temporary buffer doesn't overflow */
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS))
		goto fail;

	/* Precalculate the match array - this simplifies match loop */
	for (i = 0; i < addrsize; i++)
		initial_match_array[i] = addr ? addr[i] : 0;
	for (i = 0; i < intsize; i++)
		initial_match_array[addrsize + i] =
			cpu_to_be32(out_irq->args[i]);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
			return 0;
		}

		/*
		 * interrupt-map parsing does not work without a reg
		 * property when #address-cells != 0
		 */
		if (addrsize && !addr) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}

		/* Now look for an interrupt-map */
		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		/* No interrupt map, check for an interrupt parent */
		if (imap == NULL) {
			pr_debug(" -> no map, getting parent\n");
			newpar = of_irq_find_parent(ipar);
			goto skiplevel;
		}
		imaplen /= sizeof(u32);

		/* Look for a mask */
		imask = of_get_property(ipar, "interrupt-map-mask", NULL);
		if (!imask)
			imask = dummy_imask;

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < (addrsize + intsize); i++, imaplen--)
				match &= !((match_array[i] ^ *imap++) &
					   imask[i]);

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar =
				of_find_node_by_phandle(be32_to_cpup(imap));
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			if (!of_device_is_available(newpar))
				match = 0;

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks #interrupt-cells!\n");
				goto fail;
			}
			newintsize = be32_to_cpu(*tmp);
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
			    newintsize, newaddrsize);

			/* Check for malformed properties */
			if (WARN_ON(newaddrsize + newintsize >
				    MAX_PHANDLE_ARGS))
				goto fail;
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		/*
		 * Successfully parsed an interrrupt-map translation; copy new
		 * interrupt specifier into the out_irq structure
		 */
		out_irq->np = newpar;

		match_array = imap - newaddrsize - newintsize;
		for (i = 0; i < newintsize; i++)
			out_irq->args[i] = be32_to_cpup(imap - newintsize + i);
		out_irq->args_count = intsize = newintsize;
		addrsize = newaddrsize;

skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %s\n", of_node_full_name(newpar));
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
 fail:
	of_node_put(ipar);
	of_node_put(newpar);

	return -EINVAL;
}

static inline int of_irq_parse_oldworld(struct device_node *device, int index,
				      struct of_phandle_args *out_irq)
{
	return -EINVAL;
}
/**
 * of_irq_parse_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt for a node by walking the interrupt tree,
 * finding which interrupt controller node it is attached to, and returning the
 * interrupt specifier that can be used to retrieve a Linux IRQ number.
 */
int of_irq_parse_one(struct device_node *device, int index,
		     struct of_phandle_args *out_irq)
{
	struct device_node *p;
	const __be32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
	int i, res;

	pr_debug("of_irq_parse_one: dev=%s, index=%d\n",
		 of_node_full_name(device), index);

	/* OldWorld mac stuff is "special", handle out of line */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return of_irq_parse_oldworld(device, index, out_irq);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);

	/* Try the new-style interrupts-extended first */
	res = of_parse_phandle_with_args(device, "interrupts-extended",
					"#interrupt-cells", index, out_irq);
	if (!res)
		return of_irq_parse_raw(addr, out_irq);

	/* Get the interrupts property */
	intspec = of_get_property(device, "interrupts", &intlen);
	if (intspec == NULL)
		return -EINVAL;

	intlen /= sizeof(*intspec);

	pr_debug(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL) {
		res = -EINVAL;
		goto out;
	}
	intsize = be32_to_cpu(*tmp);

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen) {
		res = -EINVAL;
		goto out;
	}

	/* Copy intspec into irq structure */
	intspec += index * intsize;
	out_irq->np = p;
	out_irq->args_count = intsize;
	for (i = 0; i < intsize; i++)
		out_irq->args[i] = be32_to_cpup(intspec++);

	/* Check if there are any interrupt-map translations to process */
	res = of_irq_parse_raw(addr, out_irq);
 out:
	of_node_put(p);
	return res;
}
EXPORT_SYMBOL_GPL(of_irq_parse_one);

/**
 * of_irq_get - Decode a node's IRQ and return it as a Linux IRQ number
 * @dev: pointer to device tree node
 * @index: zero-based index of the IRQ
 *
 * Returns Linux IRQ number on success, or 0 on the IRQ mapping failure, or
 * -EPROBE_DEFER if the IRQ domain is not yet created, or error code in case
 * of any other failure.
 */
int of_irq_get(struct device_node *dev, int index)
{
	int rc;
	struct of_phandle_args oirq;
	struct irq_domain *domain;

	rc = of_irq_parse_one(dev, index, &oirq);
	if (rc)
		return rc;

	domain = irq_find_host(oirq.np);
	if (!domain)
		return -EPROBE_DEFER;

	return irq_create_of_mapping(oirq.np, (const u32 *)&oirq.args,
				     oirq.args_count);
}
#endif
#endif /* WILC_DEBUGFS_H */
