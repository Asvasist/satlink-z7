/* SPDX-License-Identifier: MIT */
/**
 * @file ccsds_frame_accel.h
 * @brief UAPI ioctl interface for the ccsds_frame_accel platform driver.
 *
 * Shared verbatim by the kernel driver (linux/drivers/ccsds_frame_accel) and user space
 * (linux/hal). Only one definition of every struct and ioctl number: kernel and user space
 * can never disagree about layout.
 *
 * @implements SRS-DRV-002
 */
#ifndef SATLINK_UAPI_CCSDS_FRAME_ACCEL_H
#define SATLINK_UAPI_CCSDS_FRAME_ACCEL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/** Block version, as read from the VERSION register. */
struct satlink_fa_version
{
    __u16 major;
    __u16 minor;
};

/** Control bits, as read from / written to the CTRL register. */
struct satlink_fa_ctrl
{
    __u8 randomizer_en; /**< Non-zero: XOR frames with the CCSDS pseudo-randomizer. */
    __u8 irq_en;         /**< Non-zero: raise an interrupt on FRAME_DONE. */
    __u8 reserved[2];
};

/** Snapshot of CRC / LAST_LEN / FRAME_CNT taken together, right after a FRAME_DONE event. */
struct satlink_fa_frame_stats
{
    __u16 crc;             /**< CRC-16-CCITT of the last output frame; 0 if FECF checks out. */
    __u16 reserved;
    __u32 last_len_bytes;  /**< Length of the last frame in bytes. */
    __u32 frame_cnt;       /**< Frames processed since reset (wraps). */
};

/** SATLINK_FA_IOC_WAIT_FRAME argument: block for the next FRAME_DONE, or time out. */
struct satlink_fa_wait_frame
{
    __u32 timeout_ms;              /**< in: 0 blocks forever. */
    __u32 timed_out;               /**< out: non-zero if timeout_ms elapsed with no frame. */
    struct satlink_fa_frame_stats stats; /**< out: valid only when timed_out == 0. */
};

#define SATLINK_FA_IOC_MAGIC 0xF1

#define SATLINK_FA_IOC_GET_VERSION _IOR(SATLINK_FA_IOC_MAGIC, 1, struct satlink_fa_version)
#define SATLINK_FA_IOC_GET_CTRL    _IOR(SATLINK_FA_IOC_MAGIC, 2, struct satlink_fa_ctrl)
#define SATLINK_FA_IOC_SET_CTRL    _IOW(SATLINK_FA_IOC_MAGIC, 3, struct satlink_fa_ctrl)
#define SATLINK_FA_IOC_WAIT_FRAME  _IOWR(SATLINK_FA_IOC_MAGIC, 4, struct satlink_fa_wait_frame)

#endif /* SATLINK_UAPI_CCSDS_FRAME_ACCEL_H */
