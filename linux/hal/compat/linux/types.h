/* SPDX-License-Identifier: MIT */
/**
 * @file types.h
 * @brief Stand-in for <linux/types.h> on hosts that have no kernel headers.
 *
 * Only reached when the build target is not Linux (see linux/hal/CMakeLists.txt); on Linux the
 * real header is used.
 */
#ifndef SATLINK_COMPAT_LINUX_TYPES_H
#define SATLINK_COMPAT_LINUX_TYPES_H

#include <stdint.h>

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#endif /* SATLINK_COMPAT_LINUX_TYPES_H */
