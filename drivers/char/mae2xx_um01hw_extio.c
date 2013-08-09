/*
 * mae2xx_um01hw_extio.c: Magnolia2 Ext-IO(UM01-HW) Control
 *
 * Copyright
 * Author: 2010-2012 Century Systems Co.,Ltd.
 *	   Takeyoshi Kikuchi  <kikuchi@centurysys.co.jp>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS	 SOFTWARE  IS PROVIDED	 ``AS  IS'' AND	  ANY  EXPRESS OR IMPLIED
 * WARRANTIES,	 INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.	 IN
 * NO  EVENT  SHALL   THE AUTHOR  BE	LIABLE FOR ANY	 DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED	 TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN	CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/mae2xx_um01hw_extio.h>

#define DRIVER_NAME "mae2xx_um01hw_extio"
#define EXTIO_PROC_STATUS_NAME "driver/foma_status"
#define EXTIO_PROC_PWRKEY_NAME "driver/foma_pwrkey"
#define EXTIO_PROC_SLEEP_NAME "driver/foma_sleep"

#define PROC_READ_RETURN\
	if (len <= off + count)\
		*eof = 1;\
	*start = page + off;\
	len -= off;\
\
	if (len > count)\
		len = count;\
\
	if (len < 0)\
		len = 0;\
\
	return len

static struct proc_dir_entry *proc_pwrkey = NULL;
static struct proc_dir_entry *proc_sleep = NULL;

static struct mae2xx_um01hw_extio *um01hw_extio;

static long um01hw_extio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != MAE2XX_EXTIO_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > MAE2XX_EXTIO_IOC_MAXNR)
		return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	switch (cmd) {
	case MAE2XX_EXTIO_IOCSPWRKEY:
		break;

	case MAE2XX_EXTIO_IOCGPWRKEY:
		break;

	case MAE2XX_EXTIO_IOCRESET:
		break;

	case MAE2XX_EXTIO_IOCGSTATUS:
		break;

	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static struct file_operations um01hw_extio_fops = {
	.owner = THIS_MODULE,
	.compat_ioctl = um01hw_extio_ioctl,
};

static struct miscdevice um01hw_extio_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRIVER_NAME,
	.fops = &um01hw_extio_fops,
};

/*
 *
 */
static int um01hw_extio_get_status(char *buf)
{
	char *p = buf;
	u8 *ioaddr = um01hw_extio->ioaddr;

	union foma_ctrl foma_ctrl;
	union board_status board_status;
	union foma_status foma_status;

	foma_ctrl.byte = __raw_readb(ioaddr + FOMA_CTRL);
	board_status.byte = __raw_readb(ioaddr + BOARD_STATUS);
	foma_status.byte = __raw_readb(ioaddr + FOMA_STATUS);

	p += sprintf(p, "--- UM01-HW Ext-IO ---\n");
	p += sprintf(p, " FOMA Control: 0x%02x\n", foma_ctrl.byte);
	p += sprintf(p, "  PWRKEY:	%d\n", foma_ctrl.bit.pwrkey);
	p += sprintf(p, "  SLEEP_IN:	%d\n", foma_ctrl.bit.sleep);
	p += sprintf(p, " BOARD Status: 0x%02x\n", board_status.byte);
	p += sprintf(p, " FOMA Status:	0x%02x\n", foma_status.byte);
	p += sprintf(p, "  FOTA:	%d\n", foma_status.bit.fota_n);
	p += sprintf(p, "  SLEEP_OUT:	%d\n", foma_status.bit.sleep);
	p += sprintf(p, "  MODE_LED:	%d\n", foma_status.bit.mode_led);
	p += sprintf(p, "  STATUS_LED:	%d\n", foma_status.bit.status_led);
	p += sprintf(p, "  UART_RI:	%d\n", foma_status.bit.uart_ri);
	p += sprintf(p, "  POWER_GOOD:	%d\n", foma_status.bit.power_good);
	p += sprintf(p, "  SIM_CD:	%d\n", foma_status.bit.sim_cd);
	p += sprintf(p, "--- UM02 Compatibility ---\n");
	p += sprintf(p, "  LEDG:	%d\n", foma_status.bit.sleep);

	return p - buf;
}

static int um01hw_extio_read_proc(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	int len = um01hw_extio_get_status(page);

	PROC_READ_RETURN;
}

static inline u8 get_foma_ctrl(void)
{
	u8 *base;

	base = um01hw_extio->ioaddr;

	return __raw_readb(base + FOMA_CTRL);
}

static inline void set_foma_ctrl(u8 val)
{
	u8 *base;

	base = um01hw_extio->ioaddr;

	__raw_writeb(val, base + FOMA_CTRL);
}

static int write_pwrkey(struct file *filp, const char __user *buf,
			unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val, reg;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8) simple_strtol(tmp, NULL, 10);

	reg = get_foma_ctrl();

	if (val)
		reg |= (1 << FOMA_CTRL_PWRKEY);
	else
		reg &= ~(1 << FOMA_CTRL_PWRKEY);

	set_foma_ctrl(reg);
out:
	kfree(tmp);
	return ret;
}

static int __read_foma_ctrl_reg(char *buf, int shift)
{
	char *p = buf;
	int stat;

	stat = (get_foma_ctrl() & (1 << shift)) ? 1 : 0;
	p += sprintf(p, "%d\n", stat);

	return p - buf;
}

static int read_pwrkey(char *page, char **start, off_t off,
		       int count, int *eof, void *data)
{
	unsigned long flags;
	int len;

	local_irq_save(flags);
	len = __read_foma_ctrl_reg(page, FOMA_CTRL_PWRKEY);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

static int write_foma_sleep(struct file *filp, const char __user *buf,
			    unsigned long count, void *data)
{
	int ret = count;
	char *tmp;
	u8 val, reg;

	tmp = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (count < 1) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	val = (u8) simple_strtol(tmp, NULL, 10);

	reg = get_foma_ctrl();

	if (val)
		reg |= (1 << FOMA_CTRL_SLEEP);
	else
		reg &= ~(1 << FOMA_CTRL_SLEEP);

	set_foma_ctrl(reg);
out:
	kfree(tmp);
	return ret;
}

static int read_foma_sleep(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	unsigned long flags;
	int len;

	local_irq_save(flags);
	len = __read_foma_ctrl_reg(page, FOMA_CTRL_SLEEP);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

static int um01hw_extio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct proc_dir_entry *ent;
	int len, ret;

	printk("Magnolia2 UM01-HW Ext-IO driver\n");

	um01hw_extio = kzalloc(sizeof(struct mae2xx_um01hw_extio), GFP_KERNEL);
	if (!um01hw_extio)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto err1;
	}

	len = res->end - res->start + 1;

	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto err1;
	}

	if (!create_proc_read_entry(EXTIO_PROC_STATUS_NAME, 0, 0,
				    um01hw_extio_read_proc, NULL)) {
		printk(KERN_ERR "%s: create_proc failed.\n", __FUNCTION__);
		ret = -EFAULT;
		goto err2;
	}

	ent = create_proc_entry(EXTIO_PROC_PWRKEY_NAME, S_IFREG|0644, proc_pwrkey);
	if (ent) {
		ent->write_proc = write_pwrkey;
		ent->read_proc = read_pwrkey;
	} else {
		ret = -EFAULT;
		goto err3;
	}

	ent = create_proc_entry(EXTIO_PROC_SLEEP_NAME, S_IFREG|0644, proc_sleep);
	if (ent) {
		ent->write_proc = write_foma_sleep;
		ent->read_proc = read_foma_sleep;
	} else {
		ret = -EFAULT;
		goto err4;
	}

	um01hw_extio->ioaddr = (void *) ioremap(res->start, len);
	um01hw_extio->res = res;

	misc_register(&um01hw_extio_dev);

	return 0;

err4:
	remove_proc_entry(EXTIO_PROC_PWRKEY_NAME, NULL);
err3:
	remove_proc_entry(EXTIO_PROC_STATUS_NAME, NULL);
err2:
	release_mem_region(res->start, len);
err1:
	kfree(um01hw_extio);

	return ret;
}

static int um01hw_extio_remove(struct platform_device *pdev)
{
	struct resource *res;

	res = um01hw_extio->res;

	misc_deregister(&um01hw_extio_dev);

	remove_proc_entry(EXTIO_PROC_SLEEP_NAME, NULL);
	remove_proc_entry(EXTIO_PROC_PWRKEY_NAME, NULL);
	remove_proc_entry(EXTIO_PROC_STATUS_NAME, NULL);

	iounmap(um01hw_extio->ioaddr);
	release_mem_region(res->start, res->end - res->start + 1);

	kfree(um01hw_extio);
	return 0;
}

static struct platform_driver um01hw_extio_driver = {
	.probe = um01hw_extio_probe,
	.remove = __devexit_p(um01hw_extio_remove),
	.driver = {
		.name = "um01hw_extio",
	},
};

static int __init um01hw_extio_init(void)
{
	return platform_driver_register(&um01hw_extio_driver);
}

static void __exit um01hw_extio_exit(void)
{
	return platform_driver_unregister(&um01hw_extio_driver);
}

module_init(um01hw_extio_init);
module_exit(um01hw_extio_exit);

MODULE_DESCRIPTION("Magnolia2 UM01-HW Ext-IO control driver");
MODULE_AUTHOR("Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_LICENSE("GPL");
