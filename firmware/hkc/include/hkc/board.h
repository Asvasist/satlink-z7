/**
 * @file board.h
 * @brief Board constants for the housekeeping controller. Base addresses and interrupt inputs
 *        come from the generated ICD header; only clock rates are defined here.
 *
 * @implements SRS-HKC-001
 */
#ifndef HKC_BOARD_H
#define HKC_BOARD_H

#include "satlink/regs/address_map.h"

/** AXI clock of the MicroBlaze V subsystem (FCLK_CLK0 in the block design). */
#define HKC_AXI_CLK_HZ (100000000UL)

/** Crystal on the PmodCAN (MCP2515 OSC1). */
#define HKC_MCP2515_OSC_HZ (16000000UL)

/** Bit rate of the SatLink housekeeping bus. */
#define HKC_CAN_BITRATE (500000UL)

/** Bootloader version reported on PING. */
#define HKC_BOOTLOADER_VERSION_MAJOR (1U)
#define HKC_BOOTLOADER_VERSION_MINOR (0U)

/**
 * Mailbox in the last 16 bytes of the bootloader BRAM window, excluded from both images and
 * never initialised by startup code, so it survives a jump to the reset vector.
 */
#define HKC_MAILBOX_ADDR (SATLINK_HKC_LMB_BOOTLOADER_BASE + SATLINK_HKC_LMB_BOOTLOADER_SIZE - 16U)

/** Mailbox word 0: application asks the bootloader to stay (ENTER_BOOT command). */
#define HKC_MAILBOX_STAY_MAGIC (0xB0071EADUL)

#endif /* HKC_BOARD_H */
