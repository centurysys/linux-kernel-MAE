/*
 * mae2xx_kcmv_io.c: Magnolia2 KCMV-IO Control
 *
 * Copyright
 * Author: 2012 Century Systems Co.,Ltd.
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
/*
#include <linux/miscdevice.h>
*/
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include <linux/mae2xx_kcmv_io.h>

#define DRIVER_NAME "mae2xx_kcmv_io"
#define PROC_DIR "driver/kcmv_io"
#define KCMV_IO_PROC_STATUS "status"
#define KCMV_IO_PROC_RST "rst"
#define KCMV_IO_PROC_PWR "pwr"
#define KCMV_IO_PROC_CONT1 "cont1"
#define KCMV_IO_PROC_POWER "power"
#define KCMV_IO_PROC_PWR_HOLD "pwr_hold"
#define KCMV_IO_PROC_WAKEUP "wakeup"
#define KCMV_IO_PROC_OVER_CURRENT "over_current"
#define KCMV_IO_PROC_LED "led"

#define PROC_READ_RETURN \
        if (len <= off + count) \
                *eof = 1; \
        *start = page + off; \
        len -= off; \
        if (len > count) \
                len = count; \
        if (len < 0) \
                len = 0; \
        return len

struct mae2xx_kcmv_io {
	struct resource *res;
	u8 *ioaddr;
};
static struct mae2xx_kcmv_io *kcmv_io;
static DEFINE_SPINLOCK(devlock);
static struct proc_dir_entry *proc_kcmv_io = NULL;

static inline int read_reg(int offset)
{
	u8 *base;
	base = kcmv_io->ioaddr;
	return __raw_readb(base + offset);
}

static inline void write_reg(int offset, u8 val)
{
	u8 *base;
	base = kcmv_io->ioaddr;
	__raw_writeb(val, base + offset);
}

static void led_all_off(void)
{
	union reg_led_control led_control;

	led_control.byte = read_reg(REG_LED_CONTROL);
	led_control.bit.led_g3 = 0;
	led_control.bit.led_g2 = 0;
	led_control.bit.led_g1 = 0;
	led_control.bit.led_r3 = 0;
	led_control.bit.led_r2 = 0;
	led_control.bit.led_r1 = 0;
	write_reg(REG_LED_CONTROL, led_control.byte);
}

static int dump_registers(char *buf)
{
	char *p = buf;
	u8 *ioaddr = kcmv_io->ioaddr;
	union reg_kcmv_control kcmv_control;
	union reg_board_status board_status;
	union reg_led_control led_control;
	union reg_kcmv_status kcmv_status;
	unsigned long flags;

	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = __raw_readb(ioaddr + REG_KCMV_CONTROL);
	board_status.byte = __raw_readb(ioaddr + REG_BOARD_STATUS);
	led_control.byte = __raw_readb(ioaddr + REG_LED_CONTROL);
	kcmv_status.byte = __raw_readb(ioaddr + REG_KCMV_STATUS);
	spin_unlock_irqrestore(&devlock, flags);

	p += sprintf(p, "KCMV control  :    0x%02x\n", kcmv_control.byte);
	p += sprintf(p, "          RST#:    %d\n", kcmv_control.bit.rst);
	p += sprintf(p, "          PWR#:    %d\n", kcmv_control.bit.pwr);
	p += sprintf(p, "         CONT1:    %d\n", kcmv_control.bit.cont1);
	p += sprintf(p, "   PowerSwitch:    %d\n", kcmv_control.bit.power_switch);
	p += sprintf(p, "Board status  :    0x%02x\n", board_status.byte);
	p += sprintf(p, "LED control   :    0x%02x\n", led_control.byte);
	p += sprintf(p, "        LED R1:    %d\n", led_control.bit.led_r1);
	p += sprintf(p, "        LED R2:    %d\n", led_control.bit.led_r2);
	p += sprintf(p, "        LED R3:    %d\n", led_control.bit.led_r3);
	p += sprintf(p, "        LED G1:    %d\n", led_control.bit.led_g1);
	p += sprintf(p, "        LED G2:    %d\n", led_control.bit.led_g2);
	p += sprintf(p, "        LED G3:    %d\n", led_control.bit.led_g3);
	p += sprintf(p, "KCMV status   :    0x%02x\n", kcmv_status.byte);
	p += sprintf(p, "      PWR_HOLD:    %d\n", kcmv_status.bit.pwr_hold);
	p += sprintf(p, "        WAKEUP:    %d\n", kcmv_status.bit.wakeup);
	p += sprintf(p, "   OverCurrent:    %d\n", kcmv_status.bit.over_current);
	return p - buf;
}

static int proc_read_status(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len = dump_registers(page);

	PROC_READ_RETURN;
}

static int proc_write_rst(struct file *filp, const char __user *buf,
	unsigned long count, void *data)
{
	char tmp[] = "0";
	unsigned long len = min((unsigned long)sizeof(buf)-1, count);
	u8 val;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	if (count < 1) {
		return -EFAULT;
	}
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}
	val = (u8)simple_strtol(tmp, NULL, 10);
	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	if (val)
		kcmv_control.bit.rst = 1;
	else
		kcmv_control.bit.rst = 0;
	write_reg(REG_KCMV_CONTROL, kcmv_control.byte);
	spin_unlock_irqrestore(&devlock, flags);
	return count;
}

static int proc_read_rst(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_control.bit.rst);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_write_pwr(struct file *filp, const char __user *buf,
	unsigned long count, void *data)
{
	char tmp[] = "0";
	unsigned long len = min((unsigned long)sizeof(buf)-1, count);
	u8 val;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	if (count < 1) {
		return -EFAULT;
	}
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}
	val = (u8)simple_strtol(tmp, NULL, 10);
	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	if (val)
		kcmv_control.bit.pwr = 1;
	else
		kcmv_control.bit.pwr = 0;
	write_reg(REG_KCMV_CONTROL, kcmv_control.byte);
	spin_unlock_irqrestore(&devlock, flags);
	return count;
}

static int proc_read_pwr(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_control.bit.pwr);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_write_cont1(struct file *filp, const char __user *buf,
	unsigned long count, void *data)
{
	char tmp[] = "0";
	unsigned long len = min((unsigned long)sizeof(buf)-1, count);
	u8 val;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	if (count < 1) {
		return -EFAULT;
	}
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}
	val = (u8)simple_strtol(tmp, NULL, 10);
	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	if (val)
		kcmv_control.bit.cont1 = 1;
	else
		kcmv_control.bit.cont1 = 0;
	write_reg(REG_KCMV_CONTROL, kcmv_control.byte);
	spin_unlock_irqrestore(&devlock, flags);
	return count;
}

static int proc_read_cont1(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_control.bit.cont1);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_write_power(struct file *filp, const char __user *buf,
	unsigned long count, void *data)
{
	char tmp[] = "0";
	unsigned long len = min((unsigned long)sizeof(buf)-1, count);
	u8 val;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	if (count < 1) {
		return -EFAULT;
	}
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}
	val = (u8)simple_strtol(tmp, NULL, 10);
	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	if (val)
		kcmv_control.bit.power_switch = 1;
	else
		kcmv_control.bit.power_switch = 0;
	write_reg(REG_KCMV_CONTROL, kcmv_control.byte);
	spin_unlock_irqrestore(&devlock, flags);
	return count;
}

static int proc_read_power(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_control kcmv_control;

	spin_lock_irqsave(&devlock, flags);
	kcmv_control.byte = read_reg(REG_KCMV_CONTROL);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_control.bit.power_switch);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_read_pwr_hold(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_status kcmv_status;

	spin_lock_irqsave(&devlock, flags);
	kcmv_status.byte = read_reg(REG_KCMV_STATUS);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_status.bit.pwr_hold);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_read_wakeup(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_status kcmv_status;

	spin_lock_irqsave(&devlock, flags);
	kcmv_status.byte = read_reg(REG_KCMV_STATUS);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_status.bit.wakeup);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_read_over_current(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_kcmv_status kcmv_status;

	spin_lock_irqsave(&devlock, flags);
	kcmv_status.byte = read_reg(REG_KCMV_STATUS);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "%d\n", kcmv_status.bit.over_current);
	len = p - page;
	PROC_READ_RETURN;
}

static int proc_write_led(struct file *filp, const char __user *buf,
	unsigned long count, void *data)
{
	char tmp[] = "0x00";
	unsigned long len = min((unsigned long)sizeof(tmp)-1, count);
	u8 val;
	unsigned long flags;
	union reg_led_control led_control;

	if (count < 2) {
		return -EFAULT;
	}
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
	}
	val = (u8)simple_strtol(tmp, NULL, 16);
	spin_lock_irqsave(&devlock, flags);
	led_control.byte = read_reg(REG_LED_CONTROL);
	led_control.bit.led_g3 = (val & LED_G3) ? 1 : 0;
	led_control.bit.led_g2 = (val & LED_G2) ? 1 : 0;
	led_control.bit.led_g1 = (val & LED_G1) ? 1 : 0;
	led_control.bit.led_r3 = (val & LED_R3) ? 1 : 0;
	led_control.bit.led_r2 = (val & LED_R2) ? 1 : 0;
	led_control.bit.led_r1 = (val & LED_R1) ? 1 : 0;
	write_reg(REG_LED_CONTROL, led_control.byte);
	spin_unlock_irqrestore(&devlock, flags);
	return count;
}

static int proc_read_led(char *page, char **start, off_t off,
	int count, int *eof, void *data)
{
	int len;
	char *p = page;
	unsigned long flags;
	union reg_led_control led_control;

	spin_lock_irqsave(&devlock, flags);
	led_control.byte = read_reg(REG_LED_CONTROL);
	spin_unlock_irqrestore(&devlock, flags);
	p += sprintf(p, "0x%02x\n", led_control.byte);
	len = p - page;
	PROC_READ_RETURN;
}

static int kcmv_io_create_proc_entries(void)
{
	struct proc_dir_entry *dir;
	struct proc_dir_entry *ent;

	dir = proc_mkdir(PROC_DIR, NULL);
	if (!dir)
		return -ENOMEM;
	create_proc_read_entry(KCMV_IO_PROC_STATUS, 0, dir, 
		proc_read_status, NULL);
	ent = create_proc_entry(KCMV_IO_PROC_RST, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_rst;
		ent->read_proc = proc_read_rst;
	}
	ent = create_proc_entry(KCMV_IO_PROC_PWR, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_pwr;
		ent->read_proc = proc_read_pwr;
	}
	ent = create_proc_entry(KCMV_IO_PROC_CONT1, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_cont1;
		ent->read_proc = proc_read_cont1;
	}
	ent = create_proc_entry(KCMV_IO_PROC_POWER, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_power;
		ent->read_proc = proc_read_power;
	}
	create_proc_read_entry(KCMV_IO_PROC_PWR_HOLD, 0, dir, 
		proc_read_pwr_hold, NULL);
	create_proc_read_entry(KCMV_IO_PROC_WAKEUP, 0, dir, 
		proc_read_wakeup, NULL);
	create_proc_read_entry(KCMV_IO_PROC_OVER_CURRENT, 0, dir, 
		proc_read_over_current, NULL);
	ent = create_proc_entry(KCMV_IO_PROC_LED, S_IFREG|0644, dir);
	if (ent) {
		ent->write_proc = proc_write_led;
		ent->read_proc = proc_read_led;
	}
	proc_kcmv_io = dir;
	return 0;
}

static void kcmv_io_remove_proc_entries(void)
{
	if (proc_kcmv_io == NULL) {
		return;
	}
	remove_proc_entry(KCMV_IO_PROC_LED, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_OVER_CURRENT, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_WAKEUP, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_PWR_HOLD, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_POWER, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_CONT1, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_PWR, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_RST, proc_kcmv_io);
	remove_proc_entry(KCMV_IO_PROC_STATUS, proc_kcmv_io);
	remove_proc_entry(PROC_DIR, NULL);
	proc_kcmv_io = NULL;
}

static irqreturn_t kcmv_io_irq(int irq, void *devid)
{
	return IRQ_HANDLED;
}

static int kcmv_io_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	int len;
	int irq;
	int r;

	printk("Magnolia2 KCMV-200 Ext-IO control driver\n");
	kcmv_io = kzalloc(sizeof(struct mae2xx_kcmv_io), GFP_KERNEL);
	if (!kcmv_io)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto err1;
	}
	len = res->end - res->start + 1;
	printk("%s res: %u - %u (len:%d)\n", pdev->name, res->start, res->end, len);

	if (!request_mem_region(res->start, len, pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto err1;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		goto err2;
	}

	if (kcmv_io_create_proc_entries() != 0) {
		ret = -EFAULT;
		goto err3;
	}

	kcmv_io->ioaddr = (void *)ioremap(res->start, len);
	kcmv_io->res = res;

	r = request_irq(irq, kcmv_io_irq, IRQF_TRIGGER_FALLING, pdev->name, NULL);
	if (r) {
		printk(KERN_ERR "request_irq() failed(%d).\n", r);
		goto err4;
	}

	led_all_off();
#if 0
	r = misc_register(&kcmv_io_dev);
	if (r < 0) {
		goto err5;
	}
#endif

	return 0;

err5:
	free_irq(irq, NULL);
err4:
	iounmap(kcmv_io->ioaddr);
	kcmv_io_remove_proc_entries();
err3:
err2:
	release_mem_region(res->start, len);
err1:
	kfree(kcmv_io);
	return ret;
}

static int kcmv_io_remove(struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	led_all_off();
	res = kcmv_io->res;
#if 0
	misc_deregister(&kcmv_io_dev);
#endif
	irq = platform_get_irq(pdev, 0);
	free_irq(irq, NULL);
	iounmap(kcmv_io->ioaddr);
	kcmv_io_remove_proc_entries();
	release_mem_region(res->start, res->end - res->start + 1);
	kfree(kcmv_io);
	return 0;
}

static struct platform_driver kcmv_io_driver = {
	.probe = kcmv_io_probe,
	.remove = __devexit_p(kcmv_io_remove),
	.driver = {
		.name = "kcmv_io",
	},
};

static int __init kcmv_io_init(void)
{
	return platform_driver_register(&kcmv_io_driver);
}

static void __exit kcmv_io_exit(void)
{
	platform_driver_unregister(&kcmv_io_driver);
}

module_init(kcmv_io_init);
module_exit(kcmv_io_exit);

MODULE_DESCRIPTION("Magnolia2 KCMV-200 Ext-IO control driver");
MODULE_AUTHOR("Century Systems Co.,Ltd.");
MODULE_LICENSE("GPL");
