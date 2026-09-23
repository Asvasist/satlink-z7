// SPDX-License-Identifier: GPL-2.0
/*
 * SatLink-Z7 spectrum tap platform driver.
 *
 * Register map: icd/regmap/spec_tap.yaml. The offsets below are kept in sync with it by hand,
 * for the same reason as in ccsds_frame_accel_drv.c (kernel style avoids <stdint.h>).
 *
 * The block has no interrupt of its own: the FFT output goes out through axi_dma_spec, whose
 * S2MM channel is requested here so the driver owns it from probe. Status is read with an
 * ioctl. The streaming path that consumes the FFT frames is added in Stage 5.
 *
 * @implements SRS-DRV-001
 * @implements SRS-DRV-002
 * @implements SRS-DRV-004
 */

#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <satlink/spec_tap.h>

#define ST_REG_VERSION 0x00
#define ST_REG_CTRL 0x04
#define ST_REG_STATUS 0x08
#define ST_REG_FRAME_CNT 0x0C

#define ST_CTRL_ENABLE BIT(0)
#define ST_CTRL_WINDOW_EN BIT(1)

#define ST_STATUS_BUSY BIT(0)	       /* read-only */
#define ST_STATUS_FRAME_DROPPED BIT(1) /* write 1 to clear */

#define ST_VERSION_MAJOR_SHIFT 16
#define ST_VERSION_MINOR_MASK GENMASK(15, 0)

struct satlink_st_dev {
	struct device *dev;
	void __iomem *regs;
	struct miscdevice miscdev;
	struct mutex ctrl_lock;	   /* serializes CTRL writes */
	struct dma_chan *dma_s2mm; /* FFT output; NULL until the DMA is wired up */
};

static inline struct satlink_st_dev *to_st_dev(struct file *filp)
{
	return container_of(filp->private_data, struct satlink_st_dev, miscdev);
}

static int satlink_st_open(struct inode *inode, struct file *filp)
{
	return nonseekable_open(inode, filp);
}

static long satlink_st_ioctl_get_version(struct satlink_st_dev *st, void __user *argp)
{
	u32 reg = readl(st->regs + ST_REG_VERSION);
	struct satlink_st_version ver = {
		.major = (u16)(reg >> ST_VERSION_MAJOR_SHIFT),
		.minor = (u16)(reg & ST_VERSION_MINOR_MASK),
	};

	return copy_to_user(argp, &ver, sizeof(ver)) ? -EFAULT : 0;
}

static long satlink_st_ioctl_get_ctrl(struct satlink_st_dev *st, void __user *argp)
{
	struct satlink_st_ctrl ctrl = {};
	u32 reg = readl(st->regs + ST_REG_CTRL);

	ctrl.enable = !!(reg & ST_CTRL_ENABLE);
	ctrl.window_en = !!(reg & ST_CTRL_WINDOW_EN);

	return copy_to_user(argp, &ctrl, sizeof(ctrl)) ? -EFAULT : 0;
}

static long satlink_st_ioctl_set_ctrl(struct satlink_st_dev *st, void __user *argp)
{
	struct satlink_st_ctrl ctrl;
	u32 reg = 0;

	if (copy_from_user(&ctrl, argp, sizeof(ctrl)))
		return -EFAULT;

	if (ctrl.enable)
		reg |= ST_CTRL_ENABLE;
	if (ctrl.window_en)
		reg |= ST_CTRL_WINDOW_EN;

	mutex_lock(&st->ctrl_lock);
	writel(reg, st->regs + ST_REG_CTRL);
	mutex_unlock(&st->ctrl_lock);

	return 0;
}

static long satlink_st_ioctl_get_status(struct satlink_st_dev *st, void __user *argp)
{
	struct satlink_st_status status = {};
	u32 reg = readl(st->regs + ST_REG_STATUS);

	status.busy = !!(reg & ST_STATUS_BUSY);
	status.frame_dropped = !!(reg & ST_STATUS_FRAME_DROPPED);
	status.frame_cnt = readl(st->regs + ST_REG_FRAME_CNT);

	return copy_to_user(argp, &status, sizeof(status)) ? -EFAULT : 0;
}

static long satlink_st_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct satlink_st_dev *st = to_st_dev(filp);
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case SATLINK_ST_IOC_GET_VERSION:
		return satlink_st_ioctl_get_version(st, argp);
	case SATLINK_ST_IOC_GET_CTRL:
		return satlink_st_ioctl_get_ctrl(st, argp);
	case SATLINK_ST_IOC_SET_CTRL:
		return satlink_st_ioctl_set_ctrl(st, argp);
	case SATLINK_ST_IOC_GET_STATUS:
		return satlink_st_ioctl_get_status(st, argp);
	case SATLINK_ST_IOC_CLEAR_FRAME_DROP:
		/* BUSY is read-only; writing only the W1C bit leaves it alone. */
		writel(ST_STATUS_FRAME_DROPPED, st->regs + ST_REG_STATUS);
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations satlink_st_fops = {
	.owner = THIS_MODULE,
	.open = satlink_st_open,
	.unlocked_ioctl = satlink_st_ioctl,
	.compat_ioctl = satlink_st_ioctl,
	.llseek = no_llseek,
};

static int satlink_st_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct satlink_st_dev *st;
	u32 version_reg;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->dev = dev;
	st->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(st->regs))
		return PTR_ERR(st->regs);

	mutex_init(&st->ctrl_lock);

	st->dma_s2mm = dma_request_chan(dev, "s2mm");
	if (IS_ERR(st->dma_s2mm)) {
		dev_info(dev,
			 "s2mm DMA channel not ready yet (%ld); streaming deferred to Stage 5\n",
			 PTR_ERR(st->dma_s2mm));
		st->dma_s2mm = NULL;
	}

	st->miscdev.minor = MISC_DYNAMIC_MINOR;
	st->miscdev.name = "satlink-spec-tap";
	st->miscdev.fops = &satlink_st_fops;
	st->miscdev.parent = dev;
	ret = misc_register(&st->miscdev);
	if (ret) {
		if (st->dma_s2mm)
			dma_release_channel(st->dma_s2mm);
		return dev_err_probe(dev, ret, "failed to register /dev/%s\n", st->miscdev.name);
	}

	platform_set_drvdata(pdev, st);

	version_reg = readl(st->regs + ST_REG_VERSION);
	dev_info(dev, "spec_tap v%u.%u ready, DMA %s\n", version_reg >> ST_VERSION_MAJOR_SHIFT,
		 version_reg & ST_VERSION_MINOR_MASK,
		 st->dma_s2mm ? "attached" : "not yet attached");

	return 0;
}

static int satlink_st_remove(struct platform_device *pdev)
{
	struct satlink_st_dev *st = platform_get_drvdata(pdev);

	misc_deregister(&st->miscdev);
	if (st->dma_s2mm)
		dma_release_channel(st->dma_s2mm);

	return 0;
}

static const struct of_device_id satlink_st_of_match[] = {{.compatible = "satlink,spec-tap"}, {}};
MODULE_DEVICE_TABLE(of, satlink_st_of_match);

static struct platform_driver satlink_st_driver = {
	.probe = satlink_st_probe,
	.remove = satlink_st_remove,
	.driver =
		{
			.name = "satlink-spec-tap",
			.of_match_table = satlink_st_of_match,
		},
};
module_platform_driver(satlink_st_driver);

MODULE_AUTHOR("SatLink-Z7");
MODULE_DESCRIPTION("SatLink-Z7 spectrum tap platform driver");
MODULE_LICENSE("GPL v2");
