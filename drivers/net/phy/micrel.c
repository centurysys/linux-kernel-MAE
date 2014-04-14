/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010-2013 Micrel, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : Micrel Phys:
 *		Giga phys: ksz9021, ksz9031
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>

#include <linux/netdevice.h>

/* Operation Mode Strap Override */
#define MII_KSZPHY_OMSO				0x16
#define KSZPHY_OMSO_B_CAST_OFF			(1 << 9)
#define KSZPHY_OMSO_RMII_OVERRIDE		(1 << 1)
#define KSZPHY_OMSO_MII_OVERRIDE		(1 << 0)

/* general Interrupt control/status reg in vendor specific block. */
#define MII_KSZPHY_INTCS			0x1B
#define	KSZPHY_INTCS_JABBER			(1 << 15)
#define	KSZPHY_INTCS_RECEIVE_ERR		(1 << 14)
#define	KSZPHY_INTCS_PAGE_RECEIVE		(1 << 13)
#define	KSZPHY_INTCS_PARELLEL			(1 << 12)
#define	KSZPHY_INTCS_LINK_PARTNER_ACK		(1 << 11)
#define	KSZPHY_INTCS_LINK_DOWN			(1 << 10)
#define	KSZPHY_INTCS_REMOTE_FAULT		(1 << 9)
#define	KSZPHY_INTCS_LINK_UP			(1 << 8)
#define	KSZPHY_INTCS_ALL			(KSZPHY_INTCS_LINK_UP | \
							 KSZPHY_INTCS_RECEIVE_ERR | \
							 KSZPHY_INTCS_REMOTE_FAULT | \
							 KSZPHY_INTCS_LINK_DOWN)

/* general PHY control reg in vendor specific block. */
#define	MII_KSZPHY_CTRL			0x1F
/* bitmap of PHY register to set interrupt mode */
#define KSZPHY_CTRL_INT_ACTIVE_HIGH		(1 << 9)
#define KSZ9021_CTRL_INT_ACTIVE_HIGH		(1 << 14)
#define KS8737_CTRL_INT_ACTIVE_HIGH		(1 << 14)
#define KSZ8051_RMII_50MHZ_CLK			(1 << 7)

/* 100BASE-T Status register (extend) */
#define LPA_1000MASTERSLAVE_FAULT    0x8000
#define LPA_1000LOCALPHY_MASTER      0x4000
#define LPA_1000IDLE_ERROR_COUNT(x)  ((x) & 0x00ff)

/* Write/read to/from extended registers */
#define MII_KSZPHY_EXTREG                       0x0b
#define KSZPHY_EXTREG_WRITE                     0x8000

#define MII_KSZPHY_EXTREG_WRITE                 0x0c
#define MII_KSZPHY_EXTREG_READ                  0x0d

/* Extended registers */
#define MII_KSZPHY_CLK_CONTROL_PAD_SKEW         0x104
#define MII_KSZPHY_RX_DATA_PAD_SKEW             0x105
#define MII_KSZPHY_TX_DATA_PAD_SKEW             0x106

/* Write/read to/from MMD registers */
#define MII_KSZ9031_MMD_CONTROL                 0x0d
#define MII_KSZ9031_MMD_DATA                    0x0e

/* MMD Access registers */
#define MII_KSZ9031_CLOCK_PAD_SKEW_ADDR         0x02
#define MII_KSZ9031_CLOCK_PAD_SKEW_REG          0x08

#define MMD_ACCESS(x) ((x) << 14)
#define MMD_OP_SETUP_REG   MMD_ACCESS(0x00)
#define MMD_OP_DATA_NOINC  MMD_ACCESS(0x01)
#define MMD_OP_DATA_INC_RW MMD_ACCESS(0x02)
#define MMD_OP_DATA_INC_WO MMD_ACCESS(0x03)

/* Auto MDI/MDI-X */
#define KSZ9031_AUTOMDI_MDISET  (1 << 7)
#define KSZ9031_AUTOMDI_SWAPOFF (1 << 6)

#define PS_TO_REG				200

static int ksz_config_flags(struct phy_device *phydev)
{
	int regval;

	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		regval = phy_read(phydev, MII_KSZPHY_CTRL);
		regval |= KSZ8051_RMII_50MHZ_CLK;
		return phy_write(phydev, MII_KSZPHY_CTRL, regval);
	}
	return 0;
}

static int kszphy_extended_write(struct phy_device *phydev,
				u32 regnum, u16 val)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, KSZPHY_EXTREG_WRITE | regnum);
	return phy_write(phydev, MII_KSZPHY_EXTREG_WRITE, val);
}

static int kszphy_extended_read(struct phy_device *phydev,
				u32 regnum)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, regnum);
	return phy_read(phydev, MII_KSZPHY_EXTREG_READ);
}

static int ksz9031_mmd_write(struct phy_device *phydev,
			     u16 addr, u16 reg, u16 val)
{
	/* setup mmd register's addr/reg */
	phy_write(phydev, MII_KSZ9031_MMD_CONTROL, addr | MMD_OP_SETUP_REG);
	phy_write(phydev, MII_KSZ9031_MMD_DATA, reg);

	/* write to MMD register */
	phy_write(phydev, MII_KSZ9031_MMD_CONTROL, addr | MMD_OP_DATA_NOINC);
	return phy_write(phydev, MII_KSZ9031_MMD_DATA, val);
}

static int ksz9031_mmd_read(struct phy_device *phydev,
			    u16 addr, u16 reg)
{
	/* setup mmd register's addr/reg */
	phy_write(phydev, MII_KSZ9031_MMD_CONTROL, addr | MMD_OP_SETUP_REG);
	phy_write(phydev, MII_KSZ9031_MMD_DATA, reg);

	/* read from MMD register */
	phy_write(phydev, MII_KSZ9031_MMD_CONTROL, addr | MMD_OP_DATA_NOINC);
	return phy_read(phydev, MII_KSZ9031_MMD_DATA);
}

static int kszphy_ack_interrupt(struct phy_device *phydev)
{
	/* bit[7..0] int status, which is a read and clear register. */
	int rc;

	rc = phy_read(phydev, MII_KSZPHY_INTCS);

	return (rc < 0) ? rc : 0;
}

static int kszphy_set_interrupt(struct phy_device *phydev)
{
	int temp;
	temp = (PHY_INTERRUPT_ENABLED == phydev->interrupts) ?
		KSZPHY_INTCS_ALL : 0;
	return phy_write(phydev, MII_KSZPHY_INTCS, temp);
}

static int kszphy_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KSZPHY_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int ksz9021_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KSZ9021_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int ks8737_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KS8737_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int kszphy_setup_led(struct phy_device *phydev,
			    unsigned int reg, unsigned int shift)
{

	struct device *dev = &phydev->dev;
	struct device_node *of_node = dev->of_node;
	int rc, temp;
	u32 val;

	if (!of_node && dev->parent->of_node)
		of_node = dev->parent->of_node;

	if (of_property_read_u32(of_node, "micrel,led-mode", &val))
		return 0;

	temp = phy_read(phydev, reg);
	if (temp < 0)
		return temp;

	temp &= ~(3 << shift);
	temp |= val << shift;
	rc = phy_write(phydev, reg, temp);

	return rc < 0 ? rc : 0;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	return 0;
}

static int kszphy_config_init_led8041(struct phy_device *phydev)
{
	/* single led control, register 0x1e bits 15..14 */
	return kszphy_setup_led(phydev, 0x1e, 14);
}

static int ksz8021_config_init(struct phy_device *phydev)
{
	const u16 val = KSZPHY_OMSO_B_CAST_OFF | KSZPHY_OMSO_RMII_OVERRIDE;
	int rc;

	rc = kszphy_setup_led(phydev, 0x1f, 4);
	if (rc)
		dev_err(&phydev->dev, "failed to set led mode\n");

	phy_write(phydev, MII_KSZPHY_OMSO, val);
	rc = ksz_config_flags(phydev);
	return rc < 0 ? rc : 0;
}

static int ks8051_config_init(struct phy_device *phydev)
{
	int rc;

	rc = kszphy_setup_led(phydev, 0x1f, 4);
	if (rc)
		dev_err(&phydev->dev, "failed to set led mode\n");

	rc = ksz_config_flags(phydev);
	return rc < 0 ? rc : 0;
}

static int ksz9021_load_values_from_of(struct phy_device *phydev,
				       struct device_node *of_node, u16 reg,
				       char *field1, char *field2,
				       char *field3, char *field4)
{
	int val1 = -1;
	int val2 = -2;
	int val3 = -3;
	int val4 = -4;
	int newval;
	int matches = 0;

	if (!of_property_read_u32(of_node, field1, &val1))
		matches++;

	if (!of_property_read_u32(of_node, field2, &val2))
		matches++;

	if (!of_property_read_u32(of_node, field3, &val3))
		matches++;

	if (!of_property_read_u32(of_node, field4, &val4))
		matches++;

	if (!matches)
		return 0;

	if (matches < 4)
		newval = kszphy_extended_read(phydev, reg);
	else
		newval = 0;

	if (val1 != -1)
		newval = ((newval & 0xfff0) | ((val1 / PS_TO_REG) & 0xf) << 0);

	if (val2 != -1)
		newval = ((newval & 0xff0f) | ((val2 / PS_TO_REG) & 0xf) << 4);

	if (val3 != -1)
		newval = ((newval & 0xf0ff) | ((val3 / PS_TO_REG) & 0xf) << 8);

	if (val4 != -1)
		newval = ((newval & 0x0fff) | ((val4 / PS_TO_REG) & 0xf) << 12);

	return kszphy_extended_write(phydev, reg, newval);
}

static int ksz9021_config_init(struct phy_device *phydev)
{
	struct device *dev = &phydev->dev;
	struct device_node *of_node = dev->of_node;

	if (!of_node && dev->parent->of_node)
		of_node = dev->parent->of_node;

	if (of_node) {
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_CLK_CONTROL_PAD_SKEW,
				    "txen-skew-ps", "txc-skew-ps",
				    "rxdv-skew-ps", "rxc-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_RX_DATA_PAD_SKEW,
				    "rxd0-skew-ps", "rxd1-skew-ps",
				    "rxd2-skew-ps", "rxd3-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_TX_DATA_PAD_SKEW,
				    "txd0-skew-ps", "txd1-skew-ps",
				    "txd2-skew-ps", "txd3-skew-ps");
	}
	return 0;
}

static int ksz9031_config_init(struct phy_device *phydev)
{
	struct device *dev = &phydev->dev;
	struct device_node *of_node = dev->of_node;
	u32 skew_tx, skew_rx;
	u16 val, old_val;

	if (!of_node && dev->parent->of_node)
		of_node = dev->parent->of_node;

#if 0
	val = phy_read(phydev, MII_CTRL1000);
	val |= CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER;
	phy_write(phydev, MII_CTRL1000, val);
	printk("%s: phy[%d] MII_CTRL1000 <- 0x%04x\n",
	       __FUNCTION__, phydev->addr, val);
#endif
	if (of_node) {
		val = ksz9031_mmd_read(phydev, MII_KSZ9031_CLOCK_PAD_SKEW_ADDR,
				       MII_KSZ9031_CLOCK_PAD_SKEW_REG);
		val = old_val = val & 0x03ff;

		if (!of_property_read_u32(of_node, "tx-skew", &skew_tx)) {
			val = (val & ~(0x1f << 5)) | ((skew_tx & 0x1f) << 5);
		}

		if (!of_property_read_u32(of_node, "rx-skew", &skew_rx)) {
			val = (val & ~(0x1f << 0)) | ((skew_rx & 0x1f) << 0);
		}

		if (val != old_val) {
			printk("KSZ9031: update clock-skew register: 0x%04x -> 0x%04x\n",
			       old_val, val);
			ksz9031_mmd_write(phydev, MII_KSZ9031_CLOCK_PAD_SKEW_ADDR,
					  MII_KSZ9031_CLOCK_PAD_SKEW_REG, val);
		}
	}
	return 0;
}

static int ksz9031_config_advert(struct phy_device *phydev)
{
	u32 advertise;
	int oldadv, adv;
	int err, changed = 0;

	/* Only allow advertising what
	 * this PHY supports */
	phydev->advertising &= phydev->supported;
	advertise = phydev->advertising;

	/* Setup standard advertisement */
	oldadv = adv = phy_read(phydev, MII_ADVERTISE);

	if (adv < 0)
		return adv;

	adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4 | ADVERTISE_PAUSE_CAP |
		 ADVERTISE_PAUSE_ASYM);
	adv |= ethtool_adv_to_mii_adv_t(advertise);

	if (adv != oldadv) {
		err = phy_write(phydev, MII_ADVERTISE, adv);

		if (err < 0)
			return err;
		changed = 1;
	}

	/* Configure gigabit if it's supported */
	if (phydev->supported & (SUPPORTED_1000baseT_Half |
				SUPPORTED_1000baseT_Full)) {
		oldadv = adv = phy_read(phydev, MII_CTRL1000);

		if (adv < 0)
			return adv;

		adv &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);
		adv |= ethtool_adv_to_mii_ctrl1000_t(advertise);

		if (adv != oldadv) {
			err = phy_write(phydev, MII_CTRL1000, adv);

			if (err < 0)
				return err;
			changed = 1;
		}
	}

	return changed;
}

static int ksz9031_config_aneg(struct phy_device *phydev)
{
	int result;
#ifdef CONFIG_PHY_MANUAL_MDIX
	u16 val;
#endif

	if (AUTONEG_ENABLE != phydev->autoneg)
		return genphy_setup_forced(phydev);

	result = ksz9031_config_advert(phydev);

	if (result < 0) /* error */
		return result;

#ifdef CONFIG_PHY_MANUAL_MDIX
	/* Configure Auto MDI/MDI-X */
	switch (phydev->mdix) {
	case ETH_TP_MDI_INVALID:
	default:
		phydev->mdix = ETH_TP_MDI_AUTO;
		/* FALLTHROUGH */
	case ETH_TP_MDI_AUTO:
		val = 0;
		break;

	case ETH_TP_MDI:
		val = KSZ9031_AUTOMDI_SWAPOFF | KSZ9031_AUTOMDI_MDISET;
		break;

	case ETH_TP_MDI_X:
		val = KSZ9031_AUTOMDI_SWAPOFF;
		break;
	}

	result = phy_write(phydev, MII_NCONFIG, val);

	if (result < 0)
		return result;
	else
		result = 1; /* do restart aneg */
#endif

	if (result == 0) {
		/* Advertisement hasn't changed, but maybe aneg was never on to
		 * begin with?  Or maybe phy was isolated? */
		int ctl = phy_read(phydev, MII_BMCR);

		if (ctl < 0)
			return ctl;

		if (!(ctl & BMCR_ANENABLE) || (ctl & BMCR_ISOLATE))
			result = 1; /* do restart aneg */
	}

	/* Only restart aneg if we are advertising something different
	 * than we were before.	 */
	if (result > 0)
		result = genphy_restart_aneg(phydev);

	return result;
}

int ksz9031_read_status(struct phy_device *phydev)
{
	int res;
	u16 val;

#ifdef CONFIG_PHY_MANUAL_MDIX
	/* read Auto MDI/MDI-X register */
	res = phy_read(phydev, MII_NCONFIG);
	if (res < 0)
		return res;

	val = (u16) res;

	if (!(res & KSZ9031_AUTOMDI_SWAPOFF)) {
		phydev->mdix = ETH_TP_MDI_AUTO;	/* Auto MDI/MDI-X */
	} else {
		if (res & KSZ9031_AUTOMDI_MDISET)
			phydev->mdix = ETH_TP_MDI;	/* Force MDI */
		else
			phydev->mdix = ETH_TP_MDI_X;	/* Force MDI-X */
	}
#endif
	res = genphy_read_status(phydev);

	if (res >= 0 && phydev->state == PHY_CHANGELINK) {
		if (phydev->link == 1) {
			val = phy_read(phydev, MII_STAT1000);

			if ((val & LPA_1000FULL) &&
			    LPA_1000IDLE_ERROR_COUNT(val) >= 0x7f) {
				printk("%s: Idle Error detected, configure as master...\n",
				       __FUNCTION__);
				val = phy_read(phydev, MII_CTRL1000);
				val |= CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER;
				phy_write(phydev, MII_CTRL1000, val);

				phydev->state = PHY_UP;
				res = 1;
			}
		} else {
			val = phy_read(phydev, MII_CTRL1000);
			val &= ~(CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER);
			phy_write(phydev, MII_CTRL1000, val);
		}
	}

	return res;
}

#define KSZ8873MLL_GLOBAL_CONTROL_4	0x06
#define KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX	(1 << 6)
#define KSZ8873MLL_GLOBAL_CONTROL_4_SPEED	(1 << 4)
static int ksz8873mll_read_status(struct phy_device *phydev)
{
	int regval;

	/* dummy read */
	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX)
		phydev->duplex = DUPLEX_HALF;
	else
		phydev->duplex = DUPLEX_FULL;

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_SPEED)
		phydev->speed = SPEED_10;
	else
		phydev->speed = SPEED_100;

	phydev->link = 1;
	phydev->pause = phydev->asym_pause = 0;

	return 0;
}

static int ksz8873mll_config_aneg(struct phy_device *phydev)
{
	return 0;
}

static struct phy_driver ksphy_driver[] = {
{
	.phy_id		= PHY_ID_KS8737,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KS8737",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ks8737_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8021,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8021 or KSZ8031",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause |
			   SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ksz8021_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8031,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8031",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause |
			   SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ksz8021_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8041,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8041",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init_led8041,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8041RNLI,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8041RNLI",
	.features	= PHY_BASIC_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init_led8041,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8051,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8051",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ks8051_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8001,
	.name		= "Micrel KSZ8001 or KS8721",
	.phy_id_mask	= 0x00ffffff,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init_led8041,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8081,
	.name		= "Micrel KSZ8081 or KSZ8091",
	.phy_id_mask	= 0x00fffff0,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8061,
	.name		= "Micrel KSZ8061",
	.phy_id_mask	= 0x00fffff0,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000ffffe,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ksz9021_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ksz9021_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ9031,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ9031 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ksz9031_config_init,
//	.config_aneg	= genphy_config_aneg,
	.config_aneg	= ksz9031_config_aneg,
	.read_status	= ksz9031_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ksz9021_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ8873MLL,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8873MLL Switch",
	.features	= (SUPPORTED_Pause | SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG,
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ886X,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ886X Switch",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
} };

static int __init ksphy_init(void)
{
	return phy_drivers_register(ksphy_driver,
		ARRAY_SIZE(ksphy_driver));
}

static void __exit ksphy_exit(void)
{
	phy_drivers_unregister(ksphy_driver,
		ARRAY_SIZE(ksphy_driver));
}

module_init(ksphy_init);
module_exit(ksphy_exit);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000ffffe },
	{ PHY_ID_KSZ9031, 0x00fffff0 },
	{ PHY_ID_KSZ8001, 0x00ffffff },
	{ PHY_ID_KS8737, 0x00fffff0 },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8031, 0x00ffffff },
	{ PHY_ID_KSZ8041, 0x00fffff0 },
	{ PHY_ID_KSZ8051, 0x00fffff0 },
	{ PHY_ID_KSZ8061, 0x00fffff0 },
	{ PHY_ID_KSZ8081, 0x00fffff0 },
	{ PHY_ID_KSZ8873MLL, 0x00fffff0 },
	{ PHY_ID_KSZ886X, 0x00fffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
