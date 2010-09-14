/*
 * mae2xx_flnet_driver.c: mae2xx expansion FL-net card driver (flnet)
 *
 * Copyright
 * Author: 2010 Century Systems Co.,Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <linux/mae2xx_flnet_driver.h>

MODULE_DESCRIPTION("mae2xx expansion FL-net card driver (flnet)");
MODULE_LICENSE("GPL");

/* device resource */
static struct flnet_resource *flnet_res_data;

/* transfer buffer between user area and 8 bits io memmory (64 Kbytes) */
#define TRANS_BUFF_SIZE 0x10000
static u8 *trans_buff;

/* open once variable */
static atomic_t flnet_open_once = ATOMIC_INIT(1);

/* prototype */
static int check_flwrite(struct flwrite *p);
static int check_flread(struct flread *p);
static void copy_to_dpm(int to_offset, u8 *from_buff, int size);
static void copy_from_dpm(u8 *to_buff, int from_offset, int size);
static int get_sem(void);
static void rel_sem(void);
static void write_flnetcr(u8 value);
static u8 read_flnetcr(void);


//#define DEBUG
#ifdef DEBUG
static void dump(void *data, int len);
#define deb_print(fmt, args...) printk(KERN_INFO "%s: " fmt, __func__, ## args)
#define deb_dump(data, len) dump(data, len)
#else
#define deb_print(fmt, args...) 
#define deb_dump(data, len) 
#endif


/*------------------------------------------------------------------------------
FUNCTION:	open
DESCRIPTION:
	本デバイスドライバの前提条件を下記に記載する。
	・SMP(symmetric multiprocessing)に対応しない。
	・1つのプロセスに1回のみオープンを許可する。
	・デバイスドライバへの呼び出しは、呼び出し側で排他制御を行わなければならない。
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int flnet_open(struct inode *inode, struct file *filp)
{
	int ret;

	deb_print("\n");
	if (atomic_dec_and_test(&flnet_open_once)) {
		ret = 0;
	} else {
		atomic_inc(&flnet_open_once);
		ret = -EBUSY;
	}
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	release
DESCRIPTION:
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int flnet_release(struct inode *inode, struct file *filp)
{
	deb_print("\n");
	atomic_inc(&flnet_open_once);
	return 0;
}


/*------------------------------------------------------------------------------
FUNCTION:	ioctl
DESCRIPTION:
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int flnet_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	struct flwrite flw;
	struct flread  flr;
	int value;
	u8 flnetcr;
	unsigned long flags;
	int ret;

	switch (cmd) {
	case FLNET_WRITE:
		if(copy_from_user(&flw, (struct flwrite *)arg, sizeof(struct flwrite))) {
			ret = -EFAULT;
			break;
		}
		deb_print("flnet_write, offset: %d, size: %d\n", flw.offset, flw.size);
		if ((ret = check_flwrite(&flw)) != 0) {;
			break;
		}
		if (copy_from_user(trans_buff, flw.buff, flw.size)) {
			ret = -EFAULT;
			break;
		}
#ifdef DEBUG
		flw.size > 16 ? deb_dump(trans_buff, 16) : deb_dump(trans_buff, flw.size);
#endif
		copy_to_dpm(flw.offset, trans_buff, flw.size);
		ret = 0;
		break;

	case FLNET_WRITE_SEM:
		if(copy_from_user(&flw, (struct flwrite *)arg, sizeof(struct flwrite))) {
			ret = -EFAULT;
			break;
		}
		deb_print("flnet_write_sem, offset: %d, size: %d\n", flw.offset, flw.size);
		if ((ret = check_flwrite(&flw)) != 0) {;
			break;
		}
		if (copy_from_user(trans_buff, flw.buff, flw.size)) {
			ret = -EFAULT;
			break;
		}
#ifdef DEBUG
		flw.size > 16 ? deb_dump(trans_buff, 16) : deb_dump(trans_buff, flw.size);
#endif
		local_irq_save(flags);			/* disable irq */
		if ((ret = get_sem()) != 0) {
			local_irq_restore(flags);	/* enable irq */
			break;
		}
		copy_to_dpm(flw.offset, trans_buff, flw.size);
		rel_sem();
		local_irq_restore(flags);		/* enable irq */
		ret = 0;
		break;

	case FLNET_READ:
		if(copy_from_user(&flr, (struct flread *)arg, sizeof(struct flread))) {
			ret = -EFAULT;
			break;
		}
		deb_print("flnet_read, offset: %d, size: %d\n", flr.offset, flr.size);
		if ((ret = check_flread(&flr)) != 0) {
			break;
		}
		copy_from_dpm(trans_buff, flr.offset, flr.size);
#ifdef DEBUG
		flr.size > 16 ? deb_dump(trans_buff, 16) :  deb_dump(trans_buff, flr.size);
#endif
		if (copy_to_user(flr.buff, trans_buff, flr.size)) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;

	case FLNET_READ_SEM:
		if(copy_from_user(&flr, (struct flread *)arg, sizeof(struct flread))) {
			ret = -EFAULT;
			break;
		}
		deb_print("flnet_read_sem, offset: %d, size: %d\n", flr.offset, flr.size);
		if ((ret = check_flread(&flr)) != 0) {
			break;
		}
		local_irq_save(flags);			/* disable irq */
		if ((ret = get_sem()) != 0) {
			local_irq_restore(flags);	/* enable irq */
			break;
		}
		copy_from_dpm(trans_buff, flr.offset, flr.size);
		rel_sem();
		local_irq_restore(flags);		/* enable irq */
#ifdef DEBUG
		flr.size > 16 ? deb_dump(trans_buff, 16) : deb_dump(trans_buff, flr.size);
#endif
		if (copy_to_user(flr.buff, trans_buff, flr.size)) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;

	case FLNET_SET_RESET:
		deb_print("flnet_set_reset\n");
		if(copy_from_user(&value, (int *)arg, sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		deb_print("value: %d\n", value);
		flnetcr = read_flnetcr();
		deb_print("flnetcr(before): 0x%02x\n", flnetcr);
		if (value == FLNET_RESET_ON) {			/* set reset */
			flnetcr &= ~FLNET_RESET;
		} else if (value == FLNET_RESET_OFF) {	/* set normal */
			flnetcr |= FLNET_RESET;
		} else {
			ret = -ENOTTY;
			break;
		}
		deb_print("flnetcr(after): 0x%02x\n", flnetcr);
		write_flnetcr(flnetcr);
		ret = 0;
		break;

    case FLNET_GET_RESET:
		deb_print("flnet_get_reset\n");
		flnetcr = read_flnetcr();
		deb_print("flnetcr: 0x%02x\n", flnetcr);
		if (flnetcr & FLNET_RESET) {			/* get normal */
			value = FLNET_RESET_OFF;
		} else {								/* get reset */
			value = FLNET_RESET_ON;
		}
		deb_print("value: %d\n", value);
		if (copy_to_user((int *)arg, &value,  sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;

	case FLNET_SET_CS0:
		deb_print("flnet_set_cs0\n");
		if(copy_from_user(&value, (int *)arg, sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		deb_print("value: %d\n", value);
		flnetcr = read_flnetcr();
		deb_print("flnetcr(before): 0x%02x\n", flnetcr);
		if (value == FLNET_CS0_NORMAL) {			/* set normal */
			flnetcr &= ~FLNET_MODE;
		} else if (value == FLNET_CS0_DOWNLOAD) {	/* set download */
			flnetcr |= FLNET_MODE;
		} else {
			ret = -ENOTTY;
			break;
		}
		deb_print("flnetcr(after): 0x%02x\n", flnetcr);
		write_flnetcr(flnetcr);
		ret = 0;
		break;

	case FLNET_GET_CS0:
		deb_print("flnet_get_cs0\n");
		flnetcr = read_flnetcr();
		deb_print("flnetcr: 0x%02x\n", flnetcr);
		if (flnetcr & FLNET_MODE) {					/* get download */
			value = FLNET_CS0_DOWNLOAD;
		} else {									/* get normal */
			value = FLNET_CS0_NORMAL;
		}
		deb_print("value: %d\n", value);
		if (copy_to_user((int *)arg, &value,  sizeof(int))) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;

	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}


/*------------------------------------------------------------------------------
file_operations
------------------------------------------------------------------------------*/
static struct file_operations flnet_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= flnet_ioctl,
	.open		= flnet_open,
	.release	= flnet_release,
};


/*------------------------------------------------------------------------------
miscdevice
------------------------------------------------------------------------------*/
static struct miscdevice flnet_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "flnet0",
	.fops = &flnet_fops,
};


/*------------------------------------------------------------------------------
FUNCTION:	probe
DESCRIPTION:
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int flnet_probe(struct platform_device *pdev)
{
	struct resource *res;
	int len;
	int ret;

	printk(KERN_INFO "Magnolia2 expansion FL-net card driver (flnet)\n");

	/* Allocate resource memory */
	flnet_res_data = kzalloc(sizeof(struct flnet_resource), GFP_KERNEL);
	if (!flnet_res_data) {
		printk(KERN_ERR "%s: failed to kzalloc\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	trans_buff = kzalloc(TRANS_BUFF_SIZE, GFP_KERNEL);
	if (!trans_buff) {
		printk(KERN_ERR "%s: failed to kzalloc for trans_buff\n", __func__);
		ret = -ENOMEM;
		goto err1;
	}
	deb_print("trans_buff: 0x%p\n", trans_buff);

	/* Get platform_device and resource data from "arch/arm/mach-mx35/magnolia2.c" */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_ERR "%s: failed to platform_get_resource\n", __func__);
		ret = -ENODEV;
		goto err2;
	}

	/* Allocate io memory and mapping */
	len = res->end - res->start + 1;
	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "%s: failed to request_mem_region\n", __func__);
		ret = -ENOMEM;
		goto err2;
	}
	flnet_res_data->ioaddr = (void *)ioremap(res->start, len);
	flnet_res_data->res = res;
	deb_print("flnet_res_data->res.start: 0x%08x\n", flnet_res_data->res->start);
	deb_print("flnet_res_data->res.end  : 0x%08x\n", flnet_res_data->res->end);
	deb_print("pdev->name: %s\n", pdev->name);
	deb_print("flnet_res_data->ioaddr: 0x%p\n", flnet_res_data->ioaddr);

	/* Register device driver */
	ret = misc_register(&flnet_dev);
	if (ret) {
		printk(KERN_ERR "%s: failed to misc_register\n", __func__);
		ret = -ENODEV;
		goto err3;
	}

	printk(KERN_INFO "%s: succefully loaded.\n", __func__);
	return 0;

err3:
	release_mem_region(res->start, len);
err2:
	kfree(trans_buff);
err1:
	kfree(flnet_res_data);
exit:
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	remove
DESCRIPTION:
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int flnet_remove(struct platform_device *pdev)
{
	struct resource *res;
	int len;

	printk(KERN_INFO "%s: unloaded\n", __func__); 

	misc_deregister(&flnet_dev);
	iounmap(flnet_res_data->ioaddr);
	res = flnet_res_data->res;
	len = res->end - res->start + 1;
	release_mem_region(res->start, len);
	kfree(trans_buff);
	kfree(flnet_res_data);
	return 0;
}


#if 0	/* magnolia2.c へ移動 */
/*------------------------------------------------------------------------------
resource
------------------------------------------------------------------------------*/
#define CS4_BASE_ADDR           0xB4000000

static struct resource flnet_extio_resource = {
	.start = CS4_BASE_ADDR,
	.end = CS4_BASE_ADDR + 0x0B,
	.flags = IORESOURCE_MEM,
};


/*------------------------------------------------------------------------------
platform_device
------------------------------------------------------------------------------*/
static void dummy_dev_release(struct device *dev)
{
	return;
}


static struct platform_device flnet_card_device = {
	.name = "flnet_card",
	.id = 0,
	.dev = {
		.release = dummy_dev_release,
	},
	.num_resources = 1,
	.resource = &flnet_extio_resource,
};
#endif


/*------------------------------------------------------------------------------
platform_driver
------------------------------------------------------------------------------*/
static struct platform_driver flnet_card_driver = {
	.probe = flnet_probe,
	.remove = __devexit_p(flnet_remove),
	.driver = {
		.name = "flnet_card",
	},
};


/*------------------------------------------------------------------------------
FUNCTION:	init
DESCRIPTION:
ARGUMENTS:	
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int __init flnet_init(void)
{
	int ret;

	deb_print("%s\n", __func__);
#if 0	/* magnolia2.c へ移動 */
	platform_device_register(&flnet_card_device);		// retは0か0以外
#endif
	ret = platform_driver_register(&flnet_card_driver);	// retは0か0以外
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	cleanup
DESCRIPTION:
ARGUMENTS:	
RETURN:		None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void __exit flnet_cleanup(void)
{
	deb_print("%s\n", __func__);
	platform_driver_unregister(&flnet_card_driver);
#if 0	/* magnolia2.c へ移動 */
/* magnolia2.c, but not use */
	platform_device_unregister(&flnet_card_device);
#endif
	return;
}

module_init(flnet_init);
module_exit(flnet_cleanup);


/*******************************************************************************/
/* library functions														   */
/*******************************************************************************/

/*------------------------------------------------------------------------------
FUNCTION:	check flwrite structure
DESCRIPTION:
ARGUMENTS:	
	p		flwrite structure
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int check_flwrite(struct flwrite *p)
{
	int ret;

	if ((p->offset >= FLNET_MIN_OFFSET) &&
		(p->offset <= FLNET_MAX_OFFSET) &&
		(p->size >= FLNET_MIN_SIZE) &&
		(p->size <= FLNET_MAX_SIZE) &&
		(p->offset + p->size <= FLNET_MAX_SIZE) &&
		(p->buff != NULL)) {
		ret = 0;
	} else {
		printk(KERN_ERR "%s: invalid parameter\n", __func__);
		ret = -ENOTTY;
	}
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	check flread structure
DESCRIPTION:
ARGUMENTS:	
	p		flread structure
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int check_flread(struct flread *p)
{
	return check_flwrite((struct flwrite *)p);
}


/*------------------------------------------------------------------------------
FUNCTION:	copy write data to dual-port memory
DESCRIPTION:
ARGUMENTS:	
	to_offset	dpm上の書き込み位置
	from_buff	dpm上の書き込みサイズ
	size		dpmへデータを書き込むバッファアドレス
RETURN:	None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void copy_to_dpm(int to_offset, u8 *from_buff, int size)
{
	iowrite8(ADDR_HIGH(to_offset), flnet_res_data->ioaddr + DPM_HAD);
	iowrite8(ADDR_LOW(to_offset) , flnet_res_data->ioaddr + DPM_LAD);
	iowrite8_rep(flnet_res_data->ioaddr + DPM_DAT, from_buff, size);
}


/*------------------------------------------------------------------------------
FUNCTION:	copy read data from dual-port memory
DESCRIPTION:
ARGUMENTS:	
	to_buff		dpmからデータを読み込むバッファアドレス
	from_offset	dpm上の読み込み開始位置
	size		dpm上の読み込みサイズ
RETURN:	None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void copy_from_dpm(u8 *to_buff, int from_offset, int size)
{
	iowrite8(ADDR_HIGH(from_offset), flnet_res_data->ioaddr + DPM_HAD);
	iowrite8(ADDR_LOW(from_offset) , flnet_res_data->ioaddr + DPM_LAD);
	ioread8_rep(flnet_res_data->ioaddr + DPM_DAT, to_buff, size);
}


/*------------------------------------------------------------------------------
FUNCTION:	Get semaphore of dual-port memory
DESCRIPTION:
	semaphoreを獲得できるまで待つ。
	ただし、ハードウェア障害によりsemaphoreを獲得できない場合を考慮し、タイム
	アウトエラーを設ける。
ARGUMENTS:	None
RETURN:
	正常	0
	異常	0以外
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int get_sem(void)
{
	int i;
	u8 value;

	for (i = 0; i < 10000000; i++) {
		iowrite8(ADDR_HIGH(SEM_ADDR), flnet_res_data->ioaddr + DPM_HAD);
		iowrite8(ADDR_LOW(SEM_ADDR) , flnet_res_data->ioaddr + DPM_LAD);
		iowrite8(SEM_REQ , flnet_res_data->ioaddr + SEM_DAT);
		iowrite8(ADDR_HIGH(SEM_ADDR), flnet_res_data->ioaddr + DPM_HAD);
		iowrite8(ADDR_LOW(SEM_ADDR) , flnet_res_data->ioaddr + DPM_LAD);
		value = ioread8(flnet_res_data->ioaddr + SEM_DAT) & SEM_MASK;
		if (value == SEM_GOT) {
			return 0;
		}
	}
	printk(KERN_ERR "%s: failed to get semaphore of dpm\n", __func__);
	return -EIO;
}


/*------------------------------------------------------------------------------
FUNCTION:	Release semaphore of dual-port memory
DESCRIPTION:
ARGUMENTS:	None
RETURN:		None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void rel_sem(void)
{
	iowrite8(ADDR_HIGH(SEM_ADDR), flnet_res_data->ioaddr + DPM_HAD);
	iowrite8(ADDR_LOW(SEM_ADDR) , flnet_res_data->ioaddr + DPM_LAD);
	iowrite8(SEM_REL , flnet_res_data->ioaddr + SEM_DAT);
}


/*------------------------------------------------------------------------------
FUNCTION:	Write control register value of expansion FL-net card
DESCRIPTION:
ARGUMENTS:	
	value	register value
RETURN:
	None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void write_flnetcr(u8 value)
{
	iowrite8(value, flnet_res_data->ioaddr + FLNET_CR);
}


/*------------------------------------------------------------------------------
FUNCTION:	Read control register value of expansion FL-net card
DESCRIPTION:
ARGUMENTS:	None
RETURN:
	register value
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static u8 read_flnetcr(void)
{
	return ioread8(flnet_res_data->ioaddr + FLNET_CR);
}


#ifdef DEBUG
/*------------------------------------------------------------------------------
FUNCTION:	Dump memory
------------------------------------------------------------------------------*/
static void dump(void *data, int len)
{
	int i;
	int base;
	unsigned char *buf = (unsigned char *)data;

	for (base = 0; base < len; base += 16) {
		printk("%04x ", base);
		for (i = base; i < base+16; i++) {
			if (i%16 == 8) {
				printk("- ");
			}
			if (i < len) {
				printk("%02x ", buf[i]);
			} else {
				printk("   ");
			}
		}
		printk("  ");
		for (i = base; i < base+16; i++) {
			if (i < len) {
				if (0x20 <= buf[i] && buf[i] <= 0x7f) {
					printk("%c", buf[i]);
				} else {
					printk(".");
				}
			} else {
				break;
			}
		}
		printk("\n");
	}
}
#endif
