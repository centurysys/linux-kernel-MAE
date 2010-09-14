/*
 * mae2xx_fldin_driver.c: mae2xx expansion FL-net card driver (fldin)
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
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/mae2xx_fldin_driver.h>

MODULE_DESCRIPTION("mae2xx expansion FL-net card driver (fldin)");
MODULE_LICENSE("GPL");

/* device resource */
static struct fldin_resource *fldin_res_data;

/* open once variable */
static atomic_t fldin_open_once = ATOMIC_INIT(1);

/* prototype */
static u8 read_din_status(void);
#if 0	/* 未使用 */
static void write_din_irq(u8 value);
#endif


//#define DEBUG
#ifdef DEBUG
#define deb_print(fmt, args...) printk(KERN_INFO "%s: " fmt, __func__, ## args)
#else
#define deb_print(fmt, args...) 
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
static int fldin_open(struct inode *inode, struct file *filp)
{
	int ret;

	deb_print("\n");
	if (atomic_dec_and_test(&fldin_open_once)) {
		ret = 0;
	} else {
		atomic_inc(&fldin_open_once);
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
static int fldin_release(struct inode *inode, struct file *filp)
{
	deb_print("\n");
	atomic_inc(&fldin_open_once);
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
static int fldin_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	int value;
	int ret;

	switch (cmd) {
	case FLDIN_READ:
		value = (int)read_din_status();
		deb_print("fldin_read: %d\n", value);
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
FUNCTION:	/proc read function
DESCRIPTION:
ARGUMENTS:	
RETURN:
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int fldin_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	u8 value;

	value = read_din_status();
	deb_print("value: %d\n", value);
	len += sprintf(page + len, "%d\n", value);

	/* ----------------------------------------------*/
	/* 2) Set *start = an address within the buffer. */
	/* fs/proc/generic.c / proc_file_read() 参照     */
	/* ----------------------------------------------*/
	if (len <= off + count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}


/*------------------------------------------------------------------------------
file_operations
------------------------------------------------------------------------------*/
static struct file_operations fldin_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= fldin_ioctl,
	.open		= fldin_open,
	.release	= fldin_release,
};


/*------------------------------------------------------------------------------
miscdevice
------------------------------------------------------------------------------*/
static struct miscdevice fldin_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fldin",
	.fops = &fldin_fops,
};


/*------------------------------------------------------------------------------
FUNCTION:	probe
DESCRIPTION:
ARGUMENTS:	
RETURN:
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int fldin_probe(struct platform_device *pdev)
{
	struct resource *res;
	int len;
	int ret;

	printk(KERN_INFO "Magnolia2 expansion FL-net card driver (fldin)\n");

	/* Allocate resource memory */
	fldin_res_data = kzalloc(sizeof(struct fldin_resource), GFP_KERNEL);
	if (!fldin_res_data) {
		printk(KERN_ERR "%s: failed to kzalloc\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	/* Get platform_device and resource data from "arch/arm/mach-mx35/magnolia2.c" */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_ERR "%s: failed to platform_get_resource\n", __func__);
		ret = -ENODEV;
		goto err1;
	}

	/* Allocate io memory and mapping */
	len = res->end - res->start + 1;
	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "%s: failed to request_mem_region\n", __func__);
		ret = -ENOMEM;
		goto err1;
	}
	fldin_res_data->ioaddr = (void *)ioremap(res->start, len);
	fldin_res_data->res = res;
	deb_print("fldin_res_data->res.start: 0x%08x\n", fldin_res_data->res->start);
	deb_print("fldin_res_data->res.end  : 0x%08x\n", fldin_res_data->res->end);
	deb_print("pdev->name: %s\n", pdev->name);
	deb_print("fldin_res_data->ioaddr: 0x%p\n", fldin_res_data->ioaddr);

	/* Allocate /proc read entry */
	if (! create_proc_read_entry(FLDIN_PROC_DIR, 0, 0, fldin_read_proc, NULL)) {
		printk(KERN_ERR "%s: failed to create_proc_read_entry\n", __func__);
		ret = -ENOMEM;
		goto err2;
	}

	/* Register device driver */
	ret = misc_register(&fldin_dev);
	if (ret) {
		printk(KERN_ERR "%s: failed to misc_register\n", __func__);
		ret = -ENODEV;
		goto err3;
	}

	printk(KERN_INFO "%s: succefully loaded.\n", __func__);
	return 0;

err3:
	remove_proc_entry(FLDIN_PROC_DIR, NULL);
err2:
	release_mem_region(res->start, len);
err1:
	kfree(fldin_res_data);
exit:
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	remove
DESCRIPTION:
ARGUMENTS:	
RETURN:
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int fldin_remove(struct platform_device *pdev)
{
	struct resource *res;
	int len;

	printk(KERN_INFO "%s: unloaded\n", __func__); 

	misc_deregister(&fldin_dev);
	remove_proc_entry(FLDIN_PROC_DIR, NULL);
	iounmap(fldin_res_data->ioaddr);
	res = fldin_res_data->res;
	len = res->end - res->start + 1;
	release_mem_region(res->start, len);
	kfree(fldin_res_data);
	return 0;
}


#if 0	/* magnolia2.c へ移動 */
/*------------------------------------------------------------------------------
resource
------------------------------------------------------------------------------*/
/* magnolia2.c */
#include "board-magnolia2.h"
#define CS4_BASE_ADDR           0xB4000000

static struct resource fldin_extio_resource[] = {
	{
	.start = CS4_BASE_ADDR + 0x10,
	.end = CS4_BASE_ADDR + 0x11,
	.flags = IORESOURCE_MEM,
	},
	{
	.start = MXC_INT_GPIO_P3(2),
	.end = MXC_INT_GPIO_P3(2),
	.flags = IORESOURCE_IRQ,
	}
};


/*------------------------------------------------------------------------------
platform_device
------------------------------------------------------------------------------*/
static void dummy_dev_release(struct device *dev)
{
	return;
}


static struct platform_device fldin_card_device = {
	.name = "fldin_card",
	.id = 0,
	.dev = {
		.release = dummy_dev_release,
	},
	.num_resources = 2,
	.resource = fldin_extio_resource,
};
#endif


/*------------------------------------------------------------------------------
platform_driver
------------------------------------------------------------------------------*/
static struct platform_driver fldin_card_driver = {
	.probe = fldin_probe,
	.remove = __devexit_p(fldin_remove),
	.driver = {
		.name = "fldin_card",
	},
};


/*------------------------------------------------------------------------------
FUNCTION:	init
DESCRIPTION:
ARGUMENTS:	
RETURN:
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static int __init fldin_init(void)
{
	int ret;

	deb_print("%s\n", __func__);
#if 0	/* magnolia2.c へ移動 */
	platform_device_register(&fldin_card_device);		// retは0か0以外
#endif
	ret = platform_driver_register(&fldin_card_driver);	// retは0か0以外
	return ret;
}


/*------------------------------------------------------------------------------
FUNCTION:	cleanup
DESCRIPTION:
ARGUMENTS:	
RETURN:
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void __exit fldin_cleanup(void)
{
	deb_print("%s\n", __func__);
	platform_driver_unregister(&fldin_card_driver);
#if 0	/* magnolia2.c へ移動 */
/* magnolia2.c, but not use */
	platform_device_unregister(&fldin_card_device);
#endif
	return;
}

module_init(fldin_init);
module_exit(fldin_cleanup);


/*******************************************************************************/
/* library functions														   */
/*******************************************************************************/

/*------------------------------------------------------------------------------
FUNCTION:	Read din status register of expansion FL-net card
DESCRIPTION:
ARGUMENTS:	None
RETURN:
	register value
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static u8 read_din_status(void)
{
	u8 value;
	value = ioread8(fldin_res_data->ioaddr + DIN_ST);
	return value & FLDIN_DIN_MASK;
}


#if 0	/* 未使用 */
/*------------------------------------------------------------------------------
FUNCTION:	Write din irq register of expansion FL-net card
DESCRIPTION:
ARGUMENTS:
	value
RETURN:	None
GLOBAL:
HISTORY:
------------------------------------------------------------------------------*/
static void write_din_irq(u8 value)
{
	value &= FLDIN_IRQ_MASK;
	iowrite8(value, fldin_res_data->ioaddr + DIN_IRQ_CR);
	return;
}
#endif
