/*
 *  Copyright 2008 Atmark Techno, Inc. All Rights Reserved.
 *  Copyright 2009-2010 Century Systems Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/ss.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>

#include <asm/arch/system.h>
#include <asm/arch/pcmcia.h>
#include <asm/arch/clock.h>
#include <asm/arch/gpio.h>

#define DRIVER_NAME "ma8xx_pcmcia"
#define AUTHOR      "Atmark Techno, Inc."
#define DESCRIPTION "PCMCIA driver"

#undef DEBUG
#if defined(DEBUG)
#define ENABLE_DUMP_MAP_FLAGS
#define ENABLE_DUMP_STATUS
#define DEBUG_FUNC()         printk(DRIVER_NAME ": %s()\n", __FUNCTION__)
#define DEBUG_INFO(args...)  printk(DRIVER_NAME ": " args)
#define DEBUG_ERR_LINE()     printk(DRIVER_NAME ": Error(%d)\n", __LINE__)
#else
#define DEBUG_FUNC()
#define DEBUG_INFO(args...)
#define DEBUG_ERR_LINE()
#endif
#define PRINT_INFO(args...)  printk(KERN_INFO  DRIVER_NAME ": " args)
#define PRINT_DEBUG(args...) printk(KERN_DEBUG DRIVER_NAME ": " args)
#define PRINT_ERR(args...)   printk(KERN_ERR   DRIVER_NAME ": " args)

enum {
	WINDOW_ATTR = 0,
	WINDOW_MEM,
	WINDOW_IO,
};

/* private event status flag */
#define DRV_EVENT_NONE			(0x0000)
#define DRV_EVENT_CARD_INSERTION	(0x0001)

#define NR_WINDOW 4
#define MAX_WINDOW 5
#if (MAX_WINDOW < NR_WINDOW)
#error Not support NR_WINDOW
#endif

#if defined(WINDOW_SIZE)
#undef WINDOW_SIZE
#endif
#define WINDOW_SIZE		(0x800) /* 2KByte */

typedef struct ma8xx_pcmcia_private {
	struct pcmcia_socket socket;
	struct clk *clk;
	int irq;

	/* private event status */
	u32 event;

	/* resource */
	struct resource	*res_io;
	struct resource	*res_mem;
	struct resource *res_attr;

	struct pcmcia_platform_data *plat;
} ma8xx_pcmcia_private_t;

enum {
	DRV_POWER_CONTROL_INIT = 0,
	DRV_POWER_CONTROL_ON,
	DRV_POWER_CONTROL_OFF,
};

extern void gpio_pcmcia_active(void);
extern void gpio_pcmcia_inactive(void);

static void _ma8xx_pcmcia_power_control(struct pcmcia_socket *sock, int mode)
{
	struct platform_device *pdev = sock->driver_data;
	ma8xx_pcmcia_private_t *priv = platform_get_drvdata(pdev);
	int timeout = 50;
	u32 status;

	switch(mode) {
	case DRV_POWER_CONTROL_ON:
		priv->plat->card_power_on();
		do {
			sock->ops->get_status(sock, &status);
			if (status & SS_POWERON)
				break;
			if (timeout-- < 0) {
				printk("%s: unable to activate "
                                       "socket power\n", __FUNCTION__);
				break;
			}
			mdelay(1);
		} while (1);
		break;
	case DRV_POWER_CONTROL_OFF:
	default:
		priv->plat->card_power_off();
		do {
			sock->ops->get_status(sock, &status);
			if (!(status & SS_POWERON))
				break;
			if (timeout-- < 0) {
				printk("%s: unable to remove "
                                       "socket power\n", __FUNCTION__);
				break;
			}
			mdelay(1);
		} while (1);
		break;
	}
}

static inline void
ma8xx_pcmcia_power_control_init(struct pcmcia_socket *sock)
{
	DEBUG_FUNC();
	_ma8xx_pcmcia_power_control(sock, DRV_POWER_CONTROL_INIT);
}

static inline void
ma8xx_pcmcia_power_on(struct pcmcia_socket *sock)
{
	DEBUG_FUNC();
	_ma8xx_pcmcia_power_control(sock, DRV_POWER_CONTROL_ON);
}

static inline void
ma8xx_pcmcia_power_off(struct pcmcia_socket *sock)
{
	DEBUG_FUNC();
	_ma8xx_pcmcia_power_control(sock, DRV_POWER_CONTROL_OFF);
}

static void
ma8xx_pcmcia_soft_reset(void)
{
	DEBUG_FUNC();

	_reg_PCMCIA_PGCR |= PCMCIA_PGCR_RESET;
	mdelay(2);

	_reg_PCMCIA_PGCR &= ~(PCMCIA_PGCR_RESET | PCMCIA_PGCR_LPMEN);
	_reg_PCMCIA_PGCR |= PCMCIA_PGCR_POE;
	mdelay(2);

	_reg_PCMCIA_PGCR = 0;
	_reg_PCMCIA_PGCR |= PCMCIA_PGCR_POE;
	mdelay(2);
}

static irqreturn_t
ma8xx_pcmcia_irq_handler(int irq, void *dev_id)
{
	ma8xx_pcmcia_private_t *priv = dev_id;
	struct pcmcia_socket *sock = &priv->socket;
	u32 pscr, pgsr, pipr, per;
	u32 events = 0;

	/* clear interrupt states */
	pscr = _reg_PCMCIA_PSCR;
	pgsr = _reg_PCMCIA_PGSR;
	per = _reg_PCMCIA_PER;

	_reg_PCMCIA_PSCR = pscr;
	_reg_PCMCIA_PGSR = pgsr;
	pscr &= per;

	events |= (pscr & PCMCIA_PSCR_CDC2) ? SS_DETECT : 0;
	events |= (pscr & PCMCIA_PSCR_CDC1) ? SS_DETECT : 0;
	events |= (pscr & PCMCIA_PSCR_POWC) ? SS_POWERON : 0;
	events |= (pscr & PCMCIA_PSCR_BVDC2) ? SS_STSCHG : 0;
	events |= (pscr & PCMCIA_PSCR_BVDC1) ? SS_STSCHG : 0;
	events |= (pscr & PCMCIA_PSCR_WPC) ? SS_WRPROT : 0;

	pipr = _reg_PCMCIA_PIPR;

	if (!(pipr & PCMCIA_PIPR_CD)){ 
		events |= (pscr & PCMCIA_PSCR_RDYR) ? SS_READY : 0;
		events |= (pscr & PCMCIA_PSCR_RDYF) ? SS_READY : 0;
		events |= (pscr & PCMCIA_PSCR_RDYH) ? SS_READY : 0;
		events |= (pscr & PCMCIA_PSCR_RDYL) ? SS_READY : 0;
	} else {
		_reg_PCMCIA_PER = (PCMCIA_PER_CDE1 | PCMCIA_PER_CDE2);
	}

	pcmcia_parse_events(sock, events);

	return IRQ_HANDLED;
}

static void
ma8xx_dump_map_flags(u32 flags)
{
#if defined(ENABLE_DUMP_MAP_FLAGS)
	DEBUG_INFO("map->flags: %s%s%s%s%s%s%s%s\n",
		   (flags == 0) ? "<NONE>" : "",
		   (flags & MAP_ACTIVE) ? "ACTIVE " : "",
		   (flags & MAP_16BIT) ? "16BIT " : "",
		   (flags & MAP_AUTOSZ) ? "AUTOSZ " : "",
		   (flags & MAP_0WS) ? "0WS " : "",
		   (flags & MAP_WRPROT) ? "WRPROT " : "",
		   (flags & MAP_ATTRIB) ? "ATTRIB " : "",
		   (flags & MAP_USE_WAIT) ? "USE_WAIT " : "");
#endif
}

static void
ma8xx_dump_status(u32 status)
{
#if defined(ENABLE_DUMP_STATUS)
	DEBUG_INFO("status: %08x %s%s%s%s%s%s\n", status,
		   (status & PCMCIA_PIPR_CD) ? "" : "[SS_DETECT]",
		   (status & PCMCIA_PIPR_RDY) ? "[SS_READY]" : "",
		   (status & PCMCIA_PIPR_WP) ? "[SS_WRPROT]" : "",
		   (status & PCMCIA_PIPR_BVD1) ? "[SS_STSCHG]" : "",
		   (status & PCMCIA_PIPR_POWERON) ? "[SS_POWERON]" : "",
		   (status & PCMCIA_PIPR_VS_5V) ? "" : "[SS_3VCARD]");
#endif
}

/* PCMCIA Strobe Timing Table */
struct timing_table {
	u32 length;
	u32 setup;
	u32 hold;
};

#define _CALC_CLOCK(wl, length)                 \
        ({                                      \
                u32 clock;                      \
                clock = (length * 100) / wl;    \
                clock += (clock % 10);          \
                clock /= 10;                    \
                clock;                          \
        })

#define CALC_PSL(wl, length)	(_CALC_CLOCK(wl, length) + 2)
#define CALC_PSST(wl, length)	(_CALC_CLOCK(wl, length) + 2)
#define CALC_PSHT(wl, length)	(_CALC_CLOCK(wl, length))

static void
ma8xx_set_timing(struct pcmcia_socket *sock,
                 struct timing_table *timing,
                 int map)
{
	struct platform_device *pdev = sock->driver_data;
	ma8xx_pcmcia_private_t *priv = platform_get_drvdata(pdev);
	u32 val, val_pre;
	u32 ahb_clk = clk_get_rate(priv->clk);
	u32 wl = (1000 * 1000 * 1000) / (ahb_clk / 10);

	val = val_pre = _reg_PCMCIA_POR(map);

	val &= ~(PCMCIA_POR_PSL_MASK | 
		 PCMCIA_POR_PSST_MASK | 
		 PCMCIA_POR_PSHT_MASK);

	if (timing->length) {
		val |= PCMCIA_POR_PSL(CALC_PSL(wl, timing->length));
		val |= PCMCIA_POR_PSST(CALC_PSST(wl, timing->setup));
		val |= PCMCIA_POR_PSHT(CALC_PSHT(wl, timing->hold));
	}

	_reg_PCMCIA_POR(map) = val;

        if (val_pre != val) {
                printk("%s: timing changed, length %d, setup %d, hold %d\n",
                       __FUNCTION__, timing->length, timing->setup, timing->hold);

                printk("%s: PCMCIA_POR 0x%08x -> 0x%08x\n", __FUNCTION__,
                       val_pre, val);
        }
}

static void
ma8xx_set_map(struct pcmcia_socket *sock, u32 base_addr,
              int type, int map)
{
	u32 val;

	_reg_PCMCIA_POR(map) &= ~(PCMCIA_POR_PV);
	_reg_PCMCIA_PBR(map) = base_addr;
	val = _reg_PCMCIA_POR(map);
	val &= (PCMCIA_POR_PSL_MASK | 
		PCMCIA_POR_PSST_MASK | 
		PCMCIA_POR_PSHT_MASK);
	val |= POR_BSIZE_2K | PCMCIA_POR_PPS_16 | PCMCIA_POR_PV;

	switch (type) {
	case WINDOW_MEM:
                val |= PCMCIA_POR_PRS(PCMCIA_POR_PRS_COMMON);
                break;

	case WINDOW_IO:
                val |= PCMCIA_POR_PRS(PCMCIA_POR_PRS_IO);
                break;

	case WINDOW_ATTR:
                val |= PCMCIA_POR_PRS(PCMCIA_POR_PRS_ATTRIBUTE);
                break;
	default:
                val |= PCMCIA_POR_PRS(PCMCIA_POR_PRS_ATTRIBUTE);
                break;
	}

	_reg_PCMCIA_POR(map) = val;
}

static int 
ma8xx_socket_init(struct pcmcia_socket *sock)
{
	DEBUG_FUNC();

	gpio_pcmcia_active();

	ma8xx_pcmcia_power_control_init(sock);
	ma8xx_pcmcia_soft_reset();

	return 0;
}

static int 
ma8xx_socket_suspend(struct pcmcia_socket *sock)
{
	DEBUG_FUNC();
	ma8xx_pcmcia_power_off(sock);
	return 0;
}

static int 
ma8xx_socket_get_status(struct pcmcia_socket *sock, u_int *value)
{
	u32 pipr;
	u32 status = 0;

	DEBUG_FUNC();

	pipr = _reg_PCMCIA_PIPR;

	ma8xx_dump_status(pipr);

	status |= (pipr & PCMCIA_PIPR_CD) ? 0 : SS_DETECT;
	status |= (pipr & PCMCIA_PIPR_RDY) ? SS_READY : 0;
	status |= (pipr & PCMCIA_PIPR_WP) ? SS_WRPROT : 0;
	status |= (pipr & PCMCIA_PIPR_BVD1) ? SS_STSCHG : 0;
	status |= (pipr & PCMCIA_PIPR_POWERON) ? SS_POWERON : 0;
	status |= (pipr & PCMCIA_PIPR_VS_5V) ? 0 : SS_3VCARD;

	*value = status;

	return 0;
}

static int 
ma8xx_socket_set_socket(struct pcmcia_socket *sock,
                        socket_state_t *state)
{
	DEBUG_FUNC();

	DEBUG_INFO("csc_mask: %08x flags: %08x Vcc: %d Vpp: %d io_irq: %d\n",
		   state->csc_mask, state->flags, state->Vcc, state->Vpp,
		   state->io_irq);

	if (state->flags & SS_RESET)
		ma8xx_socket_init(sock);	  

	if (state->Vcc == 0)
		ma8xx_pcmcia_power_off(sock);
	else
		ma8xx_pcmcia_power_on(sock);

	if (state->io_irq) {
		_reg_PCMCIA_PER =
                        (PCMCIA_PER_RDYLE | PCMCIA_PER_CDE1 | PCMCIA_PER_CDE2);
	} else {
		_reg_PCMCIA_PER =
                        (PCMCIA_PER_CDE1 | PCMCIA_PER_CDE2);
	}
	return 0;
}

static int 
ma8xx_socket_set_io_map(struct pcmcia_socket *sock,
                        struct pccard_io_map *map)
{
	struct platform_device *pdev = sock->driver_data;
	ma8xx_pcmcia_private_t *priv = platform_get_drvdata(pdev);
	struct timing_table timing = {
#if 0
		.length	= 165,
		.setup	= 70,
		.hold	= 20,
#else
		.length	= 400,
		.setup	= 100,
		.hold	= 50,
//		.hold	= 30,
#endif
	};

	DEBUG_FUNC();

	ma8xx_dump_map_flags(map->flags);

	if (map->speed)
		timing.length = map->speed;
	if (!(map->flags & MAP_ACTIVE))
		timing.length = 0;

	ma8xx_set_map(sock, priv->res_io->start, WINDOW_IO, map->map);
	ma8xx_set_timing(sock, &timing, map->map);

	map->start	= priv->res_io->start;
	map->stop	= priv->res_io->end;

	return 0;
}

static int 
ma8xx_socket_set_mem_map(struct pcmcia_socket *sock,
                         struct pccard_mem_map *map)
{
	struct platform_device *pdev = sock->driver_data;
	ma8xx_pcmcia_private_t *priv = platform_get_drvdata(pdev);
	struct timing_table timing = {
		.length	= 300,
		.setup	= 30,
		.hold	= 0,
	};

	DEBUG_FUNC();

	ma8xx_dump_map_flags(map->flags);

	if (map->speed)
		timing.length = map->speed;
	if (!(map->flags & MAP_ACTIVE))
		timing.length = 0;

	if (map->flags & MAP_ATTRIB) {
		ma8xx_set_map(sock, priv->res_attr->start,
                              WINDOW_ATTR, map->map);
		ma8xx_set_timing(sock, &timing, map->map);

		map->static_start = priv->res_attr->start + map->card_start;
	} else {
		ma8xx_set_map(sock, priv->res_mem->start,
                              WINDOW_MEM, map->map);
		ma8xx_set_timing(sock, &timing, map->map);

		map->static_start = priv->res_mem->start + map->card_start;
	}

	return 0;
}

static struct pccard_operations ma8xx_socket_ops = {
	.init		= ma8xx_socket_init,
	.suspend	= ma8xx_socket_suspend,
	.get_status	= ma8xx_socket_get_status,
	.set_socket	= ma8xx_socket_set_socket,
	.set_io_map	= ma8xx_socket_set_io_map,
	.set_mem_map	= ma8xx_socket_set_mem_map,
};

static int
ma8xx_pcmcia_drv_probe(struct platform_device *pdev)
{
	ma8xx_pcmcia_private_t *priv;
	struct pcmcia_platform_data *plat = pdev->dev.platform_data;
	struct pcmcia_socket *sock;
	int ret;

	DEBUG_FUNC();

	priv = kzalloc(sizeof(ma8xx_pcmcia_private_t), GFP_KERNEL);
	if (!priv) {
		DEBUG_ERR_LINE();
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);

	priv->plat		= plat;
	priv->event		= DRV_EVENT_NONE;
	priv->clk		= clk_get(&pdev->dev, "ahb_clk");
	priv->irq		= platform_get_irq(pdev, 0);
	priv->res_attr		= platform_get_resource_byname(pdev,
							       IORESOURCE_MEM,
							       "pcmcia:attr");
	priv->res_mem		= platform_get_resource_byname(pdev,
							       IORESOURCE_MEM,
							       "pcmcia:mem");
	priv->res_io		= platform_get_resource_byname(pdev,
							       IORESOURCE_MEM,
							       "pcmcia:io");

	if (!priv->res_attr || !priv->res_mem || !priv->res_io) {
		DEBUG_ERR_LINE();
		ret = -ENOENT;
		goto err_get_resource;
	}

	sock = &priv->socket;
	sock->ops		= &ma8xx_socket_ops;
	sock->resource_ops	= &pccard_static_ops;
	sock->owner		= THIS_MODULE;
	sock->dev.parent	= &pdev->dev;
	sock->features		= SS_CAP_STATIC_MAP | SS_CAP_PCCARD;
	sock->driver_data	= pdev;
	sock->map_size		= priv->res_io->end - priv->res_io->start + 1;
	sock->io_offset		= (u32)ioremap(priv->res_io->start,
					       sock->map_size);
	sock->io[0].res		= priv->res_io;
	sock->pci_irq		= priv->irq;

	ret = request_irq(priv->irq, ma8xx_pcmcia_irq_handler,
			  IRQF_DISABLED | IRQF_SHARED, DRIVER_NAME, priv);
	if (ret < 0) {
		DEBUG_ERR_LINE();
		goto err_request_irq;
	}

	ret = pcmcia_register_socket(&priv->socket);
	if (ret) {
		DEBUG_ERR_LINE();
		goto err_register_socket;
	}

	return 0;

err_register_socket:
	free_irq(priv->irq, priv);

err_request_irq:
	iounmap((void *)sock->io_offset);

err_get_resource:
	platform_set_drvdata(pdev, NULL);
	kfree(priv);

	return ret;
}

static int
ma8xx_pcmcia_drv_remove(struct platform_device *pdev)
{
	ma8xx_pcmcia_private_t *priv = platform_get_drvdata(pdev);
	struct pcmcia_socket *sock = &priv->socket;

	DEBUG_FUNC();

	pcmcia_unregister_socket(sock);
	iounmap((void *)sock->io_offset);

	free_irq(priv->irq, priv);
	kfree(priv);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int
ma8xx_pcmcia_drv_suspend(struct platform_device *pdev,
                         pm_message_t state)
{
	DEBUG_FUNC();
	return pcmcia_socket_dev_suspend(&pdev->dev, state);
}

static int
ma8xx_pcmcia_drv_resume(struct platform_device *pdev)
{
	DEBUG_FUNC();
	return pcmcia_socket_dev_resume(&pdev->dev);
}

static struct platform_driver ma8xx_pcmcia_driver = {
	.driver		= {
		.name = "ma8xx_pcmcia",
	},
	.probe		= ma8xx_pcmcia_drv_probe,
	.remove		= ma8xx_pcmcia_drv_remove,
	.suspend	= ma8xx_pcmcia_drv_suspend,
	.resume		= ma8xx_pcmcia_drv_resume,
};

static int __init
ma8xx_pcmcia_init(void)
{
	int ret;

	DEBUG_FUNC();

	ret = platform_driver_register(&ma8xx_pcmcia_driver);
	if (ret) {
		PRINT_ERR("Unable to register %s driver.\n", DRIVER_NAME);
		return ret;
	}

	return 0;
}

static void __exit
ma8xx_pcmcia_exit(void)
{
	DEBUG_FUNC();
	platform_driver_unregister(&ma8xx_pcmcia_driver);
}

module_init(ma8xx_pcmcia_init);
module_exit(ma8xx_pcmcia_exit);

MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_LICENSE("GPL");
