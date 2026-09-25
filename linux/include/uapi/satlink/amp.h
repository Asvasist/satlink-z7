/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR MIT) */
/*
 * SatLink-Z7 AMP driver user-space interface (/dev/satlink-amp).
 *
 * read():  one message per call: struct satlink_amp_msg_hdr followed by hdr.len payload bytes.
 *          Blocks until a message arrives (unless O_NONBLOCK); -EMSGSIZE if the buffer is too
 *          small (the message stays queued).
 * write(): one message per call, same layout; -EAGAIN (O_NONBLOCK) or blocks while the ring to
 *          the firmware is full.
 * poll():  POLLIN when a message is waiting, POLLOUT when there is room.
 *
 * Message types and payloads: libs/amp/include/satlink/amp/msg.h.
 *
 * @implements SRS-AMP-003
 */
#ifndef _UAPI_SATLINK_AMP_H
#define _UAPI_SATLINK_AMP_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SATLINK_AMP_MAX_PAYLOAD 512

struct satlink_amp_msg_hdr
{
    __u16 type;
    __u16 len;
};

/* Firmware states reported by SATLINK_AMP_IOC_GET_STATUS. */
#define SATLINK_AMP_STATE_OFFLINE 0 /* Core 1 held in reset */
#define SATLINK_AMP_STATE_BOOTING 1 /* released, waiting for the firmware */
#define SATLINK_AMP_STATE_RUNNING 2
#define SATLINK_AMP_STATE_FAULT   3 /* the firmware reported a fault */
#define SATLINK_AMP_STATE_CRASHED 4 /* heartbeat stopped without a fault report */

struct satlink_amp_status
{
    __u32 state;
    __u32 heartbeat;
    __u32 boot_count;
    __u32 restarts; /* automatic restarts after FAULT or CRASHED */
    __u32 fault_code;
    __u32 fault_addr;
    __u32 fw_version;   /* major << 16 | minor << 8 | patch */
    __u32 to_rtos_used; /* bytes queued in each ring */
    __u32 to_linux_used;
    __u32 rx_msgs;
    __u32 tx_msgs;
    __u32 irqs;
};

#define SATLINK_AMP_IOC_MAGIC      'S'
#define SATLINK_AMP_IOC_START      _IO(SATLINK_AMP_IOC_MAGIC, 0x40)
#define SATLINK_AMP_IOC_STOP       _IO(SATLINK_AMP_IOC_MAGIC, 0x41)
#define SATLINK_AMP_IOC_GET_STATUS _IOR(SATLINK_AMP_IOC_MAGIC, 0x42, struct satlink_amp_status)

#endif /* _UAPI_SATLINK_AMP_H */
