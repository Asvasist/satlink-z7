// SPDX-License-Identifier: GPL-2.0
/*
 * SatLink-Z7 CCSDS frame accelerator platform driver.
 *
 * Register map: icd/regmap/ccsds_frame_accel.yaml (single source of truth; the offsets below
 * are hand-kept in sync with it, matching libs/regs/include/satlink/regs/ccsds_frame_accel.h.
 * The portable header itself is not included here: it pulls in <stdint.h>, which kernel style
 * avoids in favour of <linux/types.h> u32/u16/u8. Regenerating a kernel-style variant from the
 * same YAML is a natural follow-up for tools/regmap/regmap_gen.py.
 *
 * Bulk frame data movement (the mm2s/s2mm AXI DMA channels) is requested here so the driver
 * owns them from first probe, but the streaming read/write path is added in Stage 5 once the
 * payload manager exists to drive it - see linux/README.md.
 *
 * @implements SRS-DRV-001
 * @implements SRS-DRV-002
 * @implements SRS-DRV-003
 * @implements SRS-DRV-004
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <satlink/ccsds_frame_accel.h>

#define FA_REG_VERSION 0x00
#define FA_REG_CTRL 0x04
#define FA_REG_STATUS 0x08
#define FA_REG_CRC 0x0C
#define FA_REG_LAST_LEN 0x10
#define FA_REG_FRAME_CNT 0x14

#define FA_CTRL_RANDOMIZER_EN BIT(0)
#define FA_CTRL_IRQ_EN BIT(1)

#define FA_STATUS_FRAME_DONE BIT(0)

#define FA_VERSION_MAJOR_SHIFT 16
#define FA_VERSION_MINOR_MASK GENMASK(15, 0)

#define FA_CRC_MASK GENMASK(15, 0)
#define FA_LAST_LEN_MASK GENMASK(23, 0)

struct satlink_fa_dev {
	struct device *dev;
	void __iomem *regs;
	int irq;
	struct miscdevice miscdev;

	struct mutex ctrl_lock; /* serializes GET_CTRL/SET_CTRL read-modify-write */

	spinlock_t stats_lock; /* protects last_stats and frame_ready */
	struct satlink_fa_frame_stats last_stats;
	bool frame_ready;
	wait_queue_head_t frame_wq;

	struct dma_chan *dma_mm2s; /* frame data out to the block; NULL until Stage 5 wires it */
	struct dma_chan *dma_s2mm; /* frame data back from the block */
};

static inline struct satlink_fa_dev *to_fa_dev(struct file *filp)
{
	return container_of(filp->private_data, struct satlink_fa_dev, miscdev);
}

static irqreturn_t satlink_fa_irq_handler(int irq, void *data)
{
	struct satlink_fa_dev *fa = data;
	u32 status = readl(fa->regs + FA_REG_STATUS);

	if (!(status & FA_STATUS_FRAME_DONE))
		return IRQ_NONE;

	/* Snapshot CRC/LAST_LEN/FRAME_CNT before clearing STATUS: they describe the frame that
	 * just finished, and a new frame could start updating them the moment STATUS is cleared.
	 */
	spin_lock(&fa->stats_lock);
	fa->last_stats.crc = (u16)(readl(fa->regs + FA_REG_CRC) & FA_CRC_MASK);
	fa->last_stats.reserved = 0;
	fa->last_stats.last_len_bytes = readl(fa->regs + FA_REG_LAST_LEN) & FA_LAST_LEN_MASK;
	fa->last_stats.frame_cnt = readl(fa->regs + FA_REG_FRAME_CNT);
	fa->frame_ready = true;
	spin_unlock(&fa->stats_lock);

	writel(FA_STATUS_FRAME_DONE, fa->regs + FA_REG_STATUS); /* W1C */

	wake_up_interruptible(&fa->frame_wq);
	return IRQ_HANDLED;
}

/* wait_event*()'s condition is re-evaluated on every wake-up; taking the frame here (instead of
 * just peeking) means whichever waiter's condition runs first consumes it exactly once.
 */
static bool satlink_fa_take_frame(struct satlink_fa_dev *fa, struct satlink_fa_frame_stats *out)
{
	unsigned long flags;
	bool ready;

	spin_lock_irqsave(&fa->stats_lock, flags);
	ready = fa->frame_ready;
	if (ready) {
		*out = fa->last_stats;
		fa->frame_ready = false;
	}
	spin_unlock_irqrestore(&fa->stats_lock, flags);

	return ready;
}

static int satlink_fa_open(struct inode *inode, struct file *filp)
{
	return nonseekable_open(inode, filp);
}

static long satlink_fa_ioctl_get_version(struct satlink_fa_dev *fa, void __user *argp)
{
	u32 reg = readl(fa->regs + FA_REG_VERSION);
	struct satlink_fa_version ver = {
		.major = (u16)(reg >> FA_VERSION_MAJOR_SHIFT),
		.minor = (u16)(reg & FA_VERSION_MINOR_MASK),
	};

	return copy_to_user(argp, &ver, sizeof(ver)) ? -EFAULT : 0;
}

static long satlink_fa_ioctl_get_ctrl(struct satlink_fa_dev *fa, void __user *argp)
{
	struct satlink_fa_ctrl ctrl;
	u32 reg;

	mutex_lock(&fa->ctrl_lock);
	reg = readl(fa->regs + FA_REG_CTRL);
	mutex_unlock(&fa->ctrl_lock);

	ctrl.randomizer_en = !!(reg & FA_CTRL_RANDOMIZER_EN);
	ctrl.irq_en = !!(reg & FA_CTRL_IRQ_EN);
	ctrl.reserved[0] = 0;
	ctrl.reserved[1] = 0;

	return copy_to_user(argp, &ctrl, sizeof(ctrl)) ? -EFAULT : 0;
}

static long satlink_fa_ioctl_set_ctrl(struct satlink_fa_dev *fa, void __user *argp)
{
	struct satlink_fa_ctrl ctrl;
	u32 reg = 0;

	if (copy_from_user(&ctrl, argp, sizeof(ctrl)))
		return -EFAULT;

	if (ctrl.randomizer_en)
		reg |= FA_CTRL_RANDOMIZER_EN;
	if (ctrl.irq_en)
		reg |= FA_CTRL_IRQ_EN;

	mutex_lock(&fa->ctrl_lock);
	writel(reg, fa->regs + FA_REG_CTRL);
	mutex_unlock(&fa->ctrl_lock);

	return 0;
}

static long satlink_fa_ioctl_wait_frame(struct satlink_fa_dev *fa, void __user *argp)
{
	struct satlink_fa_wait_frame wf;
	long ret;

	if (copy_from_user(&wf, argp, sizeof(wf)))
		return -EFAULT;

	if (wf.timeout_ms == 0) {
		ret = wait_event_interruptible(fa->frame_wq, satlink_fa_take_frame(fa, &wf.stats));
		if (ret)
			return ret;
		wf.timed_out = 0;
	} else {
		ret = wait_event_interruptible_timeout(fa->frame_wq,
						       satlink_fa_take_frame(fa, &wf.stats),
						       msecs_to_jiffies(wf.timeout_ms));
		if (ret < 0)
			return ret;
		wf.timed_out = (ret == 0) ? 1 : 0;
	}

	return copy_to_user(argp, &wf, sizeof(wf)) ? -EFAULT : 0;
}

static long satlink_fa_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct satlink_fa_dev *fa = to_fa_dev(filp);
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case SATLINK_FA_IOC_GET_VERSION:
		return satlink_fa_ioctl_get_version(fa, argp);
	case SATLINK_FA_IOC_GET_CTRL:
		return satlink_fa_ioctl_get_ctrl(fa, argp);
	case SATLINK_FA_IOC_SET_CTRL:
		return satlink_fa_ioctl_set_ctrl(fa, argp);
	case SATLINK_FA_IOC_WAIT_FRAME:
		return satlink_fa_ioctl_wait_frame(fa, argp);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations satlink_fa_fops = {
	.owner = THIS_MODULE,
	.open = satlink_fa_open,
	.unlocked_ioctl = satlink_fa_ioctl,
	.compat_ioctl = satlink_fa_ioctl,
	.llseek = no_llseek,
};

static void satlink_fa_request_dma(struct satlink_fa_dev *fa)
{
	fa->dma_mm2s = dma_request_chan(fa->dev, "mm2s");
	if (IS_ERR(fa->dma_mm2s)) {
		dev_info(fa->dev,
			 "mm2s DMA channel not ready yet (%ld); streaming deferred to Stage 5\n",
			 PTR_ERR(fa->dma_mm2s));
		fa->dma_mm2s = NULL;
	}

	fa->dma_s2mm = dma_request_chan(fa->dev, "s2mm");
	if (IS_ERR(fa->dma_s2mm)) {
		dev_info(fa->dev,
			 "s2mm DMA channel not ready yet (%ld); streaming deferred to Stage 5\n",
			 PTR_ERR(fa->dma_s2mm));
		fa->dma_s2mm = NULL;
	}
}

static int satlink_fa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct satlink_fa_dev *fa;
	u32 version_reg;
	int ret;

	fa = devm_kzalloc(dev, sizeof(*fa), GFP_KERNEL);
	if (!fa)
		return -ENOMEM;

	fa->dev = dev;
	fa->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fa->regs))
		return PTR_ERR(fa->regs);

	fa->irq = platform_get_irq(pdev, 0);
	if (fa->irq < 0)
		return fa->irq;

	mutex_init(&fa->ctrl_lock);
	spin_lock_init(&fa->stats_lock);
	init_waitqueue_head(&fa->frame_wq);

	ret = devm_request_irq(dev, fa->irq, satlink_fa_irq_handler, 0, dev_name(dev), fa);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ %d\n", fa->irq);

	satlink_fa_request_dma(fa);

	fa->miscdev.minor = MISC_DYNAMIC_MINOR;
	fa->miscdev.name = "satlink-ccsds-frame-accel";
	fa->miscdev.fops = &satlink_fa_fops;
	fa->miscdev.parent = dev;
	ret = misc_register(&fa->miscdev);
	if (ret) {
		if (fa->dma_mm2s)
			dma_release_channel(fa->dma_mm2s);
		if (fa->dma_s2mm)
			dma_release_channel(fa->dma_s2mm);
		return dev_err_probe(dev, ret, "failed to register /dev/%s\n", fa->miscdev.name);
	}

	platform_set_drvdata(pdev, fa);

	version_reg = readl(fa->regs + FA_REG_VERSION);
	dev_info(dev, "ccsds_frame_accel v%u.%u ready, irq %d, DMA %s\n",
		 version_reg >> FA_VERSION_MAJOR_SHIFT, (u32)(version_reg & FA_VERSION_MINOR_MASK),
		 fa->irq, (fa->dma_mm2s && fa->dma_s2mm) ? "attached" : "not yet attached");

	return 0;
}

static int satlink_fa_remove(struct platform_device *pdev)
{
	struct satlink_fa_dev *fa = platform_get_drvdata(pdev);

	misc_deregister(&fa->miscdev);
	if (fa->dma_mm2s)
		dma_release_channel(fa->dma_mm2s);
	if (fa->dma_s2mm)
		dma_release_channel(fa->dma_s2mm);

	return 0;
}

static const struct of_device_id satlink_fa_of_match[] = {
	{.compatible = "satlink,ccsds-frame-accel"}, {}};
MODULE_DEVICE_TABLE(of, satlink_fa_of_match);

static struct platform_driver satlink_fa_driver = {
	.probe = satlink_fa_probe,
	.remove = satlink_fa_remove,
	.driver =
		{
			.name = "satlink-ccsds-frame-accel",
			.of_match_table = satlink_fa_of_match,
		},
};
module_platform_driver(satlink_fa_driver);

MODULE_AUTHOR("SatLink-Z7");
MODULE_DESCRIPTION("SatLink-Z7 CCSDS frame accelerator platform driver");
MODULE_LICENSE("GPL v2");
