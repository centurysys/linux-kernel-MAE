/*
 * ma8xx_foma_extio.c: Magnolia2 Ext-IO(FOMA) Control
 *
 * Copyright
 * Author: 2010 Century Systems Co.,Ltd.
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
#include <linux/ma8xx_foma_extio.h>

#define DRIVER_NAME "ma8xx_foma_extio"
#define EXTIO_PROC_STATUS_NAME "driver/foma_status"
#define EXTIO_PROC_PWRKEY_NAME "driver/foma_pwrkey"
#define EXTIO_PROC_SYSRST_NAME "driver/foma_reset"

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
static struct proc_dir_entry *proc_sysrst = NULL;

static struct ma8xx_foma_extio *foma_extio;

static long foma_extio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != MA8XX_EXTIO_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > MA8XX_EXTIO_IOC_MAXNR)
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
	case MA8XX_EXTIO_IOCSPWRKEY:
		break;

	case MA8XX_EXTIO_IOCGPWRKEY:
		break;

	case MA8XX_EXTIO_IOCRESET:
		break;

	case MA8XX_EXTIO_IOCGSTATUS:
		break;

	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

static struct file_operations foma_extio_fops = {
	.owner = THIS_MODULE,
	.compat_ioctl = foma_extio_ioctl,
};

static struct miscdevice foma_extio_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRIVER_NAME,
	.fops = &foma_extio_fops,
};

/*
 *
 */
static int foma_extio_get_status(char *buf)
{
	char *p = buf;
	u8 *ioaddr = foma_extio->ioaddr;

	union foma_ctrl foma_ctrl;
	union board_status board_status;
	union foma_status foma_status;

	foma_ctrl.byte = __raw_readb(ioaddr + FOMA_CTRL);
	board_status.byte = __raw_readb(ioaddr + BOARD_STATUS);
	foma_status.byte = __raw_readb(ioaddr + FOMA_STATUS);

	p += sprintf(p, "--- FOMA Ext-IO ---\n");
	p += sprintf(p, " FOMA Control: 0x%02x\n", foma_ctrl.byte);
	p += sprintf(p, "  PWRKEY:	%d\n", foma_ctrl.bit.pwrkey);
	p += sprintf(p, "  SYSRST:	%d\n", foma_ctrl.bit.sysrst);
	p += sprintf(p, "  16C550Reset: %d\n", foma_ctrl.bit.reset_16550);
	p += sprintf(p, " BOARD Status: 0x%02x\n", board_status.byte);
	p += sprintf(p, " FOMA Status:	0x%02x\n", foma_status.byte);
	p += sprintf(p, "  LEDGMS:	%d\n", foma_status.bit.led_gms);
	p += sprintf(p, "  LEDR:	%d\n", foma_status.bit.led_r);
	p += sprintf(p, "  LEDG:	%d\n", foma_status.bit.led_g);
	p += sprintf(p, "  SIM_CD:	%d\n", foma_status.bit.sim_cd);
	p += sprintf(p, "  ANT[1..3]:	[%d, %d, %d]\n",
		     foma_status.bit.ant1, foma_status.bit.ant2, foma_status.bit.ant3);
	p += sprintf(p, "  PACKET:	%d\n", foma_status.bit.packet);

	return p - buf;
}

static int foma_extio_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int len = foma_extio_get_status(page);

	PROC_READ_RETURN;
}

static inline u8 get_foma_ctrl(void)
{
	u8 *base;

	base = foma_extio->ioaddr;

	return __raw_readb(base + FOMA_CTRL);
}

static inline void set_foma_ctrl(u8 val)
{
	u8 *base;

	base = foma_extio->ioaddr;

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

static int write_foma_reset(struct file *filp, const char __user *buf,
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
		reg |= (1 << FOMA_CTRL_SYSRST);
	else
		reg &= ~(1 << FOMA_CTRL_SYSRST);

	set_foma_ctrl(reg);
out:
	kfree(tmp);
	return ret;
}

static int read_foma_reset(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	unsigned long flags;
	int len;

	local_irq_save(flags);
	len = __read_foma_ctrl_reg(page, FOMA_CTRL_SYSRST);
	local_irq_restore(flags);

	PROC_READ_RETURN;
}

extern int magnolia2_get_led_mode(void);
static int foma_extio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct proc_dir_entry *ent;
	int len, ret, mode_dme;

	mode_dme = magnolia2_get_led_mode();

	printk("Magnolia2 FOMA Ext-IO driver%s\n",
	       (mode_dme == 0 ? "" : " (DME special mode enabled)"));

	foma_extio = kzalloc(sizeof(struct ma8xx_foma_extio), GFP_KERNEL);
	if (!foma_extio)
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
				    foma_extio_read_proc, NULL)) {
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

	ent = create_proc_entry(EXTIO_PROC_SYSRST_NAME, S_IFREG|0644, proc_sysrst);
	if (ent) {
		ent->write_proc = write_foma_reset;
		ent->read_proc = read_foma_reset;
	} else {
		ret = -EFAULT;
		goto err4;
	}

	foma_extio->ioaddr = (void *) ioremap(res->start, len);
	foma_extio->res = res;

	if (mode_dme != 0)
		__raw_writeb(0x7f, foma_extio->ioaddr + LED_CTRL);

	misc_register(&foma_extio_dev);

	return 0;

err4:
	remove_proc_entry(EXTIO_PROC_PWRKEY_NAME, NULL);
err3:
	remove_proc_entry(EXTIO_PROC_STATUS_NAME, NULL);
err2:
	release_mem_region(res->start, len);
err1:
	kfree(foma_extio);

	return ret;
}

static int foma_extio_remove(struct platform_device *pdev)
{
	struct resource *res;

	res = foma_extio->res;

	misc_deregister(&foma_extio_dev);

	remove_proc_entry(EXTIO_PROC_SYSRST_NAME, NULL);
	remove_proc_entry(EXTIO_PROC_PWRKEY_NAME, NULL);
	remove_proc_entry(EXTIO_PROC_STATUS_NAME, NULL);

	iounmap(foma_extio->ioaddr);
	release_mem_region(res->start, res->end - res->start + 1);

	kfree(foma_extio);
	return 0;
}

static struct platform_driver foma_extio_driver = {
	.probe = foma_extio_probe,
	.remove = __devexit_p(foma_extio_remove),
	.driver = {
		.name = "foma_extio",
	},
};

static int __init foma_extio_init(void)
{
	return platform_driver_register(&foma_extio_driver);
}

static void __exit foma_extio_exit(void)
{
	return platform_driver_unregister(&foma_extio_driver);
}

module_init(foma_extio_init);
module_exit(foma_extio_exit);

MODULE_DESCRIPTION("Magnolia2 FOMA Ext-IO control driver");
MODULE_AUTHOR("Takeyoshi Kikuchi <kikuchi@centurysys.co.jp>");
MODULE_LICENSE("GPL");
