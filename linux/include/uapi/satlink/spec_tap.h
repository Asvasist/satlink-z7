/* SPDX-License-Identifier: MIT */
/**
 * @file spec_tap.h
 * @brief UAPI ioctl interface for the spec_tap platform driver.
 *
 * Shared by the kernel driver (linux/drivers/spec_tap) and user space (linux/hal).
 *
 * @implements SRS-DRV-002
 */
#ifndef SATLINK_UAPI_SPEC_TAP_H
#define SATLINK_UAPI_SPEC_TAP_H

#include <linux/ioctl.h>
#include <linux/types.h>

/** Block version, as read from the VERSION register. */
struct satlink_st_version
{
    __u16 major;
    __u16 minor;
};

/** Control bits, as read from / written to the CTRL register. */
struct satlink_st_ctrl
{
    __u8 enable;    /**< Non-zero: capture 1024-sample frames for the FFT. */
    __u8 window_en; /**< Non-zero: apply the Hann window (zero = rectangular). */
    __u8 reserved[2];
};

/** Capture status. */
struct satlink_st_status
{
    __u8 busy;          /**< A frame is currently being sent to the FFT. */
    __u8 frame_dropped; /**< Sticky: a frame was dropped because the FFT was not ready. */
    __u8 reserved[2];
    __u32 frame_cnt; /**< Frames sent to the FFT since reset (wraps). */
};

#define SATLINK_ST_IOC_MAGIC 0xF2

#define SATLINK_ST_IOC_GET_VERSION      _IOR(SATLINK_ST_IOC_MAGIC, 1, struct satlink_st_version)
#define SATLINK_ST_IOC_GET_CTRL         _IOR(SATLINK_ST_IOC_MAGIC, 2, struct satlink_st_ctrl)
#define SATLINK_ST_IOC_SET_CTRL         _IOW(SATLINK_ST_IOC_MAGIC, 3, struct satlink_st_ctrl)
#define SATLINK_ST_IOC_GET_STATUS       _IOR(SATLINK_ST_IOC_MAGIC, 4, struct satlink_st_status)
#define SATLINK_ST_IOC_CLEAR_FRAME_DROP _IO(SATLINK_ST_IOC_MAGIC, 5)

#endif /* SATLINK_UAPI_SPEC_TAP_H */
