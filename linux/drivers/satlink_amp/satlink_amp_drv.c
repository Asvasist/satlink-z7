// SPDX-License-Identifier: GPL-2.0
/*
 * SatLink-Z7 AMP driver: runs the FreeRTOS modem firmware on Cortex-A9 Core 1 and carries the
 * messages between it and Linux user space.
 *
 * Why not remoteproc/RPMsg: the Zynq-7000 remoteproc driver is gone from AMD's 6.6 kernel
 * (only the ZynqMP/Versal R5 drivers remain), and mainline never had one. This driver keeps
 * the same division of labour with less machinery (ADR-0005):
 *
 *   - Firmware: request_firmware(), ELF32 program headers checked against the reserved-memory
 *     regions Core 1 owns, then copied in.
 *   - Core 1 life cycle: taken from the Linux scheduler with remove_cpu(), held in reset through
 *     the SLCR (A9_CPU_RST_CTRL), released through the same trampoline at physical address 0
 *     that the Zynq SMP code uses (the kernel keeps that page reserved on Zynq).
 *   - IPC: the control block and two single-producer/single-consumer rings in ipc_shm (the ring
 *     code is libs/amp/src/ipc_ring.c, shared verbatim with the firmware and the host tests).
 *   - Doorbells: two unused PL-to-PS SPIs that each side sets pending in the GIC distributor;
 *     Linux uses irq_set_irqchip_state() for its side.
 *   - FDIR: a watchdog on the firmware heartbeat; a FAULT report or a stalled heartbeat restarts
 *     Core 1 (auto_restart parameter).
 *
 * @implements SRS-AMP-001
 * @implements SRS-AMP-002
 * @implements SRS-AMP-005
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>

#include <satlink/amp.h>
#include "satlink/amp/ipc_ring.h"
#include "satlink/amp/shm.h"

#define SLCR_A9_CPU_RST_CTRL 0x244
#define SLCR_A9_RST1 BIT(1)
#define SLCR_A9_CLKSTOP1 BIT(5)

#define AMP_CPU 1
#define BOOT_TIMEOUT_MS 2000
#define WATCHDOG_PERIOD_MS 500
#define HEARTBEAT_TIMEOUT_MS 3000

static bool auto_restart = true;
module_param(auto_restart, bool, 0644);
MODULE_PARM_DESC(auto_restart, "Restart Core 1 after a firmware fault or a stalled heartbeat");

static char *firmware = "satlink-rtos.elf";
module_param(firmware, charp, 0444);
MODULE_PARM_DESC(firmware, "Firmware file in /lib/firmware");

struct satlink_amp_region {
	phys_addr_t base;
	resource_size_t size;
};

struct satlink_amp {
	struct device *dev;
	struct miscdevice miscdev;
	struct regmap *slcr;

	struct satlink_amp_region fw;  /* rtos_fw */
	struct satlink_amp_region shm; /* ipc_shm */
	struct satlink_amp_region dma; /* modem_dma */
	void *shm_va;
	volatile satlink_shm_ctrl_t *ctrl;

	satlink_ring_t to_rtos;	 /* producer */
	satlink_ring_t to_linux; /* consumer */
	struct mutex tx_lock;	 /* one writer on to_rtos */
	struct mutex rx_lock;	 /* one reader on to_linux */
	wait_queue_head_t rx_wq;
	wait_queue_head_t tx_wq;

	int irq_to_linux; /* requested */
	int irq_to_rtos;  /* only set pending */
	u32 hwirq_to_linux;
	u32 hwirq_to_rtos;

	struct mutex state_lock; /* serializes start/stop */
	u32 state;
	u32 restarts;
	u32 last_heartbeat;
	unsigned long heartbeat_jiffies;
	struct delayed_work watchdog;
	bool took_cpu;

	atomic_t rx_msgs;
	atomic_t tx_msgs;
	atomic_t irqs;
};

static inline struct satlink_amp *to_amp(struct file *filp)
{
	return container_of(filp->private_data, struct satlink_amp, miscdev);
}

/* ---- Core 1 control ---- */

static void amp_cpu_hold(struct satlink_amp *amp)
{
	regmap_update_bits(amp->slcr, SLCR_A9_CPU_RST_CTRL, SLCR_A9_RST1 | SLCR_A9_CLKSTOP1,
			   SLCR_A9_RST1 | SLCR_A9_CLKSTOP1);
}

/* Same trampoline as arch/arm/mach-zynq/headsmp.S: load the entry address and jump. */
static const u32 amp_trampoline[] = {
	0xe59ff000, /* ldr pc, [pc]  (pc reads 8 ahead: the word after the next one) */
	0xe320f000, /* nop */
	0x00000000, /* entry address, patched in */
};

static int amp_cpu_release(struct satlink_amp *amp, u32 entry)
{
	u32 *zero;

	/* Physical address 0 is DDR on the Zybo and part of the kernel's linear map; the Zynq
	 * platform code reserves the first 512 KiB for exactly this trampoline. Write it through
	 * the normal cached mapping and clean it to memory (L1 and the outer L2), as
	 * zynq_cpun_start() does, because Core 1 fetches it with its caches off. */
	zero = memremap(0, PAGE_SIZE, MEMREMAP_WB);
	if (!zero)
		return -ENOMEM;
	memcpy(zero, amp_trampoline, sizeof(amp_trampoline));
	zero[2] = entry;
	__cpuc_flush_dcache_area(zero, sizeof(amp_trampoline));
	outer_flush_range(0, sizeof(amp_trampoline));
	memunmap(zero);

	regmap_update_bits(amp->slcr, SLCR_A9_CPU_RST_CTRL, SLCR_A9_RST1, 0);
	regmap_update_bits(amp->slcr, SLCR_A9_CPU_RST_CTRL, SLCR_A9_CLKSTOP1, 0);
	return 0;
}

static bool amp_region_contains(const struct satlink_amp_region *r, u32 addr, u32 size)
{
	return addr >= r->base && size <= r->size && addr - r->base <= r->size - size;
}

/* Copy the PT_LOAD segments of an ELF32 ARM executable into the Core 1 regions. */
static int amp_load_elf(struct satlink_amp *amp, const struct firmware *fw, u32 *entry)
{
	const struct elf32_hdr *eh = (const struct elf32_hdr *)fw->data;
	const struct elf32_phdr *ph;
	int i;

	if (fw->size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
	    eh->e_ident[EI_CLASS] != ELFCLASS32 || eh->e_machine != EM_ARM ||
	    eh->e_type != ET_EXEC || eh->e_phentsize != sizeof(*ph) || eh->e_phoff > fw->size ||
	    eh->e_phnum > (fw->size - eh->e_phoff) / sizeof(*ph)) {
		dev_err(amp->dev, "%s: not an ARM ELF32 executable\n", firmware);
		return -EINVAL;
	}
	if (!amp_region_contains(&amp->fw, eh->e_entry, 4)) {
		dev_err(amp->dev, "entry 0x%08x outside rtos_fw\n", eh->e_entry);
		return -EINVAL;
	}

	ph = (const struct elf32_phdr *)(fw->data + eh->e_phoff);
	for (i = 0; i < eh->e_phnum; i++, ph++) {
		const struct satlink_amp_region *r;
		void *dst;

		if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
			continue;
		if (ph->p_filesz > ph->p_memsz || ph->p_offset > fw->size ||
		    ph->p_filesz > fw->size - ph->p_offset) {
			dev_err(amp->dev, "segment %d: bad sizes\n", i);
			return -EINVAL;
		}
		if (amp_region_contains(&amp->fw, ph->p_paddr, ph->p_memsz))
			r = &amp->fw;
		else if (amp_region_contains(&amp->dma, ph->p_paddr, ph->p_memsz) &&
			 ph->p_filesz == 0)
			r = &amp->dma; /* NOLOAD DMA buffers: nothing to copy */
		else {
			dev_err(amp->dev, "segment %d at 0x%08x+0x%x is not Core 1 memory\n", i,
				ph->p_paddr, ph->p_memsz);
			return -EINVAL;
		}
		if (r == &amp->dma)
			continue;

		dst = memremap(ph->p_paddr, ph->p_memsz, MEMREMAP_WC);
		if (!dst)
			return -ENOMEM;
		memcpy(dst, fw->data + ph->p_offset, ph->p_filesz);
		memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
		wmb();
		memunmap(dst);
	}
	*entry = eh->e_entry;
	return 0;
}

static void amp_init_shm(struct satlink_amp *amp)
{
	void *base = amp->shm_va;

	memset(base, 0, SATLINK_SHM_TO_RTOS_OFFSET);
	amp->ctrl->version = SATLINK_SHM_VERSION;
	amp->ctrl->rtos_state = SATLINK_RTOS_STATE_OFFLINE;
	amp->ctrl->boot_count = amp->ctrl->boot_count + 1;
	amp->ctrl->irq_to_rtos = amp->hwirq_to_rtos;
	amp->ctrl->irq_to_linux = amp->hwirq_to_linux;
	satlink_ring_init(&amp->to_rtos, base + SATLINK_SHM_TO_RTOS_OFFSET, SATLINK_SHM_RING_BYTES);
	satlink_ring_init(&amp->to_linux, base + SATLINK_SHM_TO_LINUX_OFFSET,
			  SATLINK_SHM_RING_BYTES);
	wmb();
	amp->ctrl->magic = SATLINK_SHM_MAGIC; /* last: the firmware attaches only when it sees it */
	wmb();
}

static int amp_start_locked(struct satlink_amp *amp)
{
	const struct firmware *fw;
	unsigned long deadline;
	u32 entry;
	int ret;

	if (amp->state == SATLINK_AMP_STATE_RUNNING || amp->state == SATLINK_AMP_STATE_BOOTING)
		return -EBUSY;

	if (cpu_online(AMP_CPU)) {
		ret = remove_cpu(AMP_CPU);
		if (ret) {
			dev_err(amp->dev, "cannot take CPU%d offline: %d\n", AMP_CPU, ret);
			return ret;
		}
		amp->took_cpu = true;
	}
	amp_cpu_hold(amp);

	ret = request_firmware(&fw, firmware, amp->dev);
	if (ret) {
		dev_err(amp->dev, "cannot load %s: %d\n", firmware, ret);
		return ret;
	}
	/* Old image lines in the shared L2 must not be written back over the new one. */
	outer_flush_range(amp->fw.base, amp->fw.base + amp->fw.size);
	ret = amp_load_elf(amp, fw, &entry);
	release_firmware(fw);
	if (ret)
		return ret;
	outer_flush_range(amp->fw.base, amp->fw.base + amp->fw.size);

	amp_init_shm(amp);
	amp->state = SATLINK_AMP_STATE_BOOTING;
	ret = amp_cpu_release(amp, entry);
	if (ret)
		return ret;

	deadline = jiffies + msecs_to_jiffies(BOOT_TIMEOUT_MS);
	while (READ_ONCE(amp->ctrl->rtos_state) != SATLINK_RTOS_STATE_RUNNING) {
		if (time_after(jiffies, deadline)) {
			dev_err(amp->dev, "firmware did not report RUNNING (state %u)\n",
				READ_ONCE(amp->ctrl->rtos_state));
			amp_cpu_hold(amp);
			amp->state = SATLINK_AMP_STATE_OFFLINE;
			return -ETIMEDOUT;
		}
		msleep(10);
	}
	amp->state = SATLINK_AMP_STATE_RUNNING;
	amp->last_heartbeat = READ_ONCE(amp->ctrl->heartbeat);
	amp->heartbeat_jiffies = jiffies;
	dev_info(amp->dev, "Core 1 firmware %u.%u.%u running (boot %u)\n",
		 amp->ctrl->fw_version >> 16, (amp->ctrl->fw_version >> 8) & 0xff,
		 amp->ctrl->fw_version & 0xff, amp->ctrl->boot_count);
	schedule_delayed_work(&amp->watchdog, msecs_to_jiffies(WATCHDOG_PERIOD_MS));
	return 0;
}

static void amp_stop_locked(struct satlink_amp *amp)
{
	amp_cpu_hold(amp);
	if (amp->ctrl)
		amp->ctrl->rtos_state = SATLINK_RTOS_STATE_OFFLINE;
	amp->state = SATLINK_AMP_STATE_OFFLINE;
	wake_up_interruptible(&amp->rx_wq);
	wake_up_interruptible(&amp->tx_wq);
}

static void amp_watchdog(struct work_struct *work)
{
	struct satlink_amp *amp = container_of(to_delayed_work(work), struct satlink_amp, watchdog);
	u32 hb;

	mutex_lock(&amp->state_lock);
	if (amp->state != SATLINK_AMP_STATE_RUNNING) {
		mutex_unlock(&amp->state_lock);
		return;
	}
	hb = READ_ONCE(amp->ctrl->heartbeat);
	if (READ_ONCE(amp->ctrl->rtos_state) == SATLINK_RTOS_STATE_FAULT) {
		dev_err(amp->dev, "firmware fault: code %u at 0x%08x\n", amp->ctrl->fault_code,
			amp->ctrl->fault_addr);
		amp->state = SATLINK_AMP_STATE_FAULT;
	} else if (hb != amp->last_heartbeat) {
		amp->last_heartbeat = hb;
		amp->heartbeat_jiffies = jiffies;
	} else if (time_after(jiffies,
			      amp->heartbeat_jiffies + msecs_to_jiffies(HEARTBEAT_TIMEOUT_MS))) {
		dev_err(amp->dev, "firmware heartbeat stopped\n");
		amp->state = SATLINK_AMP_STATE_CRASHED;
	}

	if (amp->state != SATLINK_AMP_STATE_RUNNING) {
		amp_cpu_hold(amp);
		wake_up_interruptible(&amp->rx_wq);
		if (auto_restart) {
			amp->restarts++;
			amp->state = SATLINK_AMP_STATE_OFFLINE;
			if (amp_start_locked(amp))
				dev_err(amp->dev, "restart %u failed\n", amp->restarts);
		}
	} else {
		schedule_delayed_work(&amp->watchdog, msecs_to_jiffies(WATCHDOG_PERIOD_MS));
	}
	mutex_unlock(&amp->state_lock);
}

/* ---- Doorbells ---- */

static irqreturn_t amp_irq(int irq, void *data)
{
	struct satlink_amp *amp = data;

	atomic_inc(&amp->irqs);
	wake_up_interruptible(&amp->rx_wq);
	return IRQ_HANDLED;
}

static void amp_ring_doorbell(struct satlink_amp *amp)
{
	irq_set_irqchip_state(amp->irq_to_rtos, IRQCHIP_STATE_PENDING, true);
}

/* ---- Char device ---- */

static bool amp_rx_ready(struct satlink_amp *amp)
{
	return amp->state != SATLINK_AMP_STATE_RUNNING || satlink_ring_used(&amp->to_linux) > 0;
}

static ssize_t amp_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct satlink_amp *amp = to_amp(filp);
	struct satlink_amp_msg_hdr hdr;
	u8 *payload;
	u16 len = SATLINK_AMP_MAX_PAYLOAD;
	int rc;

	if (count < sizeof(hdr))
		return -EINVAL;
	payload = kmalloc(SATLINK_AMP_MAX_PAYLOAD, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	for (;;) {
		if (amp->state != SATLINK_AMP_STATE_RUNNING) {
			rc = -ENODEV;
			break;
		}
		mutex_lock(&amp->rx_lock);
		len = SATLINK_AMP_MAX_PAYLOAD;
		rc = satlink_ring_read(&amp->to_linux, &hdr.type, NULL, payload, &len);
		mutex_unlock(&amp->rx_lock);
		if (rc == SATLINK_RING_OK) {
			break;
		} else if (rc != SATLINK_RING_EMPTY) {
			dev_err(amp->dev, "ring from Core 1 is corrupt (%d)\n", rc);
			rc = -EIO;
			break;
		}
		if (filp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}
		rc = wait_event_interruptible(amp->rx_wq, amp_rx_ready(amp));
		if (rc)
			break;
	}
	if (rc) {
		kfree(payload);
		return rc;
	}

	hdr.len = len;
	if (count < sizeof(hdr) + len) {
		kfree(payload);
		return -EMSGSIZE; /* message consumed: caller must size buffers for the maximum */
	}
	if (copy_to_user(buf, &hdr, sizeof(hdr)) || copy_to_user(buf + sizeof(hdr), payload, len)) {
		kfree(payload);
		return -EFAULT;
	}
	kfree(payload);
	atomic_inc(&amp->rx_msgs);
	return sizeof(hdr) + len;
}

static ssize_t amp_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	struct satlink_amp *amp = to_amp(filp);
	struct satlink_amp_msg_hdr hdr;
	u8 *payload;
	int rc;

	if (count < sizeof(hdr) || copy_from_user(&hdr, buf, sizeof(hdr)))
		return -EFAULT;
	if (hdr.len > SATLINK_AMP_MAX_PAYLOAD || count != sizeof(hdr) + hdr.len)
		return -EINVAL;
	payload = memdup_user(buf + sizeof(hdr), hdr.len);
	if (IS_ERR(payload))
		return PTR_ERR(payload);

	for (;;) {
		if (amp->state != SATLINK_AMP_STATE_RUNNING) {
			rc = -ENODEV;
			break;
		}
		mutex_lock(&amp->tx_lock);
		rc = satlink_ring_write(&amp->to_rtos, hdr.type, payload, hdr.len);
		mutex_unlock(&amp->tx_lock);
		if (rc == SATLINK_RING_OK) {
			amp_ring_doorbell(amp);
			break;
		} else if (rc != SATLINK_RING_FULL) {
			rc = -EINVAL;
			break;
		}
		if (filp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}
		/* The firmware drains within its 10 ms loop; poll for room. */
		rc = wait_event_interruptible_timeout(amp->tx_wq, false, msecs_to_jiffies(5));
		if (rc < 0)
			break;
	}
	kfree(payload);
	if (rc)
		return rc;
	atomic_inc(&amp->tx_msgs);
	return count;
}

static __poll_t amp_poll(struct file *filp, poll_table *wait)
{
	struct satlink_amp *amp = to_amp(filp);
	__poll_t mask = 0;

	poll_wait(filp, &amp->rx_wq, wait);
	if (amp->state != SATLINK_AMP_STATE_RUNNING)
		return EPOLLERR;
	if (satlink_ring_used(&amp->to_linux) > 0)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (satlink_ring_free(&amp->to_rtos) > SATLINK_RING_MAX_PAYLOAD + 2 * SATLINK_RING_REC_HDR)
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

static long amp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct satlink_amp *amp = to_amp(filp);
	struct satlink_amp_status st;
	int ret = 0;

	switch (cmd) {
	case SATLINK_AMP_IOC_START:
		mutex_lock(&amp->state_lock);
		ret = amp_start_locked(amp);
		mutex_unlock(&amp->state_lock);
		return ret;
	case SATLINK_AMP_IOC_STOP:
		cancel_delayed_work_sync(&amp->watchdog);
		mutex_lock(&amp->state_lock);
		amp_stop_locked(amp);
		mutex_unlock(&amp->state_lock);
		return 0;
	case SATLINK_AMP_IOC_GET_STATUS:
		memset(&st, 0, sizeof(st));
		mutex_lock(&amp->state_lock);
		st.state = amp->state;
		st.restarts = amp->restarts;
		if (amp->ctrl->magic == SATLINK_SHM_MAGIC) {
			st.heartbeat = amp->ctrl->heartbeat;
			st.boot_count = amp->ctrl->boot_count;
			st.fault_code = amp->ctrl->fault_code;
			st.fault_addr = amp->ctrl->fault_addr;
			st.fw_version = amp->ctrl->fw_version;
			st.to_rtos_used = satlink_ring_used(&amp->to_rtos);
			st.to_linux_used = satlink_ring_used(&amp->to_linux);
		}
		mutex_unlock(&amp->state_lock);
		st.rx_msgs = atomic_read(&amp->rx_msgs);
		st.tx_msgs = atomic_read(&amp->tx_msgs);
		st.irqs = atomic_read(&amp->irqs);
		return copy_to_user((void __user *)arg, &st, sizeof(st)) ? -EFAULT : 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations amp_fops = {
	.owner = THIS_MODULE,
	.read = amp_read,
	.write = amp_write,
	.poll = amp_poll,
	.unlocked_ioctl = amp_ioctl,
	.llseek = noop_llseek,
};

/* ---- sysfs: state (read), start/stop (write) ---- */

static const char *const amp_state_names[] = {"offline", "booting", "running", "fault", "crashed"};

static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct satlink_amp *amp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n",
			  amp->state < ARRAY_SIZE(amp_state_names) ? amp_state_names[amp->state]
								   : "?");
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t count)
{
	struct satlink_amp *amp = dev_get_drvdata(dev);
	int ret = 0;

	if (sysfs_streq(buf, "start")) {
		mutex_lock(&amp->state_lock);
		ret = amp_start_locked(amp);
		mutex_unlock(&amp->state_lock);
	} else if (sysfs_streq(buf, "stop")) {
		cancel_delayed_work_sync(&amp->watchdog);
		mutex_lock(&amp->state_lock);
		amp_stop_locked(amp);
		mutex_unlock(&amp->state_lock);
	} else {
		ret = -EINVAL;
	}
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(state);

static ssize_t restarts_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct satlink_amp *amp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", amp->restarts);
}
static DEVICE_ATTR_RO(restarts);

static struct attribute *amp_attrs[] = {&dev_attr_state.attr, &dev_attr_restarts.attr, NULL};
ATTRIBUTE_GROUPS(amp);

/* ---- probe ---- */

static int amp_get_region(struct device *dev, const char *name, struct satlink_amp_region *r)
{
	struct device_node *np;
	struct resource res;
	int idx, ret;

	idx = of_property_match_string(dev->of_node, "memory-region-names", name);
	if (idx < 0)
		return dev_err_probe(dev, idx, "memory-region '%s' missing\n", name);
	np = of_parse_phandle(dev->of_node, "memory-region", idx);
	if (!np)
		return -EINVAL;
	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret)
		return ret;
	r->base = res.start;
	r->size = resource_size(&res);
	return 0;
}

static int amp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct satlink_amp *amp;
	struct irq_data *d;
	int ret;

	amp = devm_kzalloc(dev, sizeof(*amp), GFP_KERNEL);
	if (!amp)
		return -ENOMEM;
	amp->dev = dev;
	mutex_init(&amp->tx_lock);
	mutex_init(&amp->rx_lock);
	mutex_init(&amp->state_lock);
	init_waitqueue_head(&amp->rx_wq);
	init_waitqueue_head(&amp->tx_wq);
	INIT_DELAYED_WORK(&amp->watchdog, amp_watchdog);
	amp->state = SATLINK_AMP_STATE_OFFLINE;

	amp->slcr = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(amp->slcr))
		return dev_err_probe(dev, PTR_ERR(amp->slcr), "no SLCR syscon\n");

	ret = amp_get_region(dev, "rtos_fw", &amp->fw);
	if (!ret)
		ret = amp_get_region(dev, "ipc_shm", &amp->shm);
	if (!ret)
		ret = amp_get_region(dev, "modem_dma", &amp->dma);
	if (ret)
		return ret;
	if (amp->shm.size < SATLINK_SHM_SIZE)
		return dev_err_probe(dev, -EINVAL, "ipc_shm smaller than %lu\n", SATLINK_SHM_SIZE);

	/* Non-cacheable, like Core 1's view (the MMU there maps it normal non-cacheable). */
	amp->shm_va = devm_memremap(dev, amp->shm.base, SATLINK_SHM_SIZE, MEMREMAP_WC);
	if (IS_ERR(amp->shm_va))
		return PTR_ERR(amp->shm_va);
	amp->ctrl = (volatile satlink_shm_ctrl_t *)amp->shm_va;
	amp->ctrl->magic = 0;

	amp->irq_to_linux = platform_get_irq_byname(pdev, "to-linux");
	amp->irq_to_rtos = platform_get_irq_byname(pdev, "to-rtos");
	if (amp->irq_to_linux < 0 || amp->irq_to_rtos < 0)
		return dev_err_probe(dev, -EINVAL, "interrupts 'to-linux' and 'to-rtos' needed\n");
	d = irq_get_irq_data(amp->irq_to_linux);
	amp->hwirq_to_linux = d ? irqd_to_hwirq(d) : 0;
	d = irq_get_irq_data(amp->irq_to_rtos);
	amp->hwirq_to_rtos = d ? irqd_to_hwirq(d) : 0;
	ret = devm_request_irq(dev, amp->irq_to_linux, amp_irq, 0, dev_name(dev), amp);
	if (ret)
		return ret;

	amp->miscdev.minor = MISC_DYNAMIC_MINOR;
	amp->miscdev.name = "satlink-amp";
	amp->miscdev.fops = &amp_fops;
	amp->miscdev.parent = dev;
	ret = misc_register(&amp->miscdev);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, amp);

	dev_info(dev, "rtos_fw %pa, ipc_shm %pa, doorbells SPI %u/%u\n", &amp->fw.base,
		 &amp->shm.base, amp->hwirq_to_linux, amp->hwirq_to_rtos);

	if (of_property_read_bool(dev->of_node, "satlink,auto-boot")) {
		mutex_lock(&amp->state_lock);
		ret = amp_start_locked(amp);
		mutex_unlock(&amp->state_lock);
		if (ret)
			dev_warn(dev, "auto-boot failed (%d); start with /sys/.../state\n", ret);
	}
	return 0;
}

static void amp_remove(struct platform_device *pdev)
{
	struct satlink_amp *amp = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&amp->watchdog);
	mutex_lock(&amp->state_lock);
	amp_stop_locked(amp);
	mutex_unlock(&amp->state_lock);
	misc_deregister(&amp->miscdev);
	/* CPU1 stays offline: bringing it back to Linux needs add_cpu() and a normal SMP boot,
	 * which the firmware's use of its memory would have to be fenced from first. */
}

static const struct of_device_id amp_of_match[] = {{.compatible = "satlink,amp"}, {/* sentinel */}};
MODULE_DEVICE_TABLE(of, amp_of_match);

static struct platform_driver amp_driver = {
	.probe = amp_probe,
	.remove_new = amp_remove,
	.driver =
		{
			.name = "satlink-amp",
			.of_match_table = amp_of_match,
			.dev_groups = amp_groups,
		},
};
module_platform_driver(amp_driver);

MODULE_DESCRIPTION("SatLink-Z7 AMP: FreeRTOS on Cortex-A9 Core 1, shared-memory IPC");
MODULE_LICENSE("GPL");
