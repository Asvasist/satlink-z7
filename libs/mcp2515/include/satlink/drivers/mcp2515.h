/**
 * @file mcp2515.h
 * @brief Portable driver for the Microchip MCP2515 stand-alone CAN controller (PmodCAN).
 *
 * Used by the MicroBlaze V housekeeping controller on PmodCAN #2. (PmodCAN #1 belongs to Linux,
 * which uses the mainline mcp251x driver.) The driver knows nothing about the SPI controller:
 * it calls one full-duplex transfer function with chip select held for the whole transfer. The
 * firmware passes the AXI Quad SPI routine, the host unit tests a register-level model of the
 * chip.
 *
 * Datasheet: Microchip DS20001801 (MCP2515).
 *
 * @implements SRS-HKC-001
 */
#ifndef SATLINK_DRIVERS_MCP2515_H
#define SATLINK_DRIVERS_MCP2515_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/status.h"
#include "satlink/hkc/can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPI instructions (datasheet table 12-1). */
#define MCP2515_SPI_RESET       (0xC0U)
#define MCP2515_SPI_READ        (0x03U)
#define MCP2515_SPI_READ_RXB0   (0x90U) /**< READ RX BUFFER at RXB0SIDH; clears RX0IF on CS rise. */
#define MCP2515_SPI_READ_RXB1   (0x94U) /**< READ RX BUFFER at RXB1SIDH; clears RX1IF on CS rise. */
#define MCP2515_SPI_WRITE       (0x02U)
#define MCP2515_SPI_LOAD_TXB0   (0x40U) /**< LOAD TX BUFFER starting at TXB0SIDH. */
#define MCP2515_SPI_RTS_TXB0    (0x81U)
#define MCP2515_SPI_READ_STATUS (0xA0U)
#define MCP2515_SPI_BIT_MODIFY  (0x05U)

/* Registers. */
#define MCP2515_REG_RXF0SIDH (0x00U)
#define MCP2515_REG_CANSTAT  (0x0EU)
#define MCP2515_REG_CANCTRL  (0x0FU)
#define MCP2515_REG_TEC      (0x1CU)
#define MCP2515_REG_REC      (0x1DU)
#define MCP2515_REG_RXM0SIDH (0x20U)
#define MCP2515_REG_RXM1SIDH (0x24U)
#define MCP2515_REG_CNF3     (0x28U)
#define MCP2515_REG_CNF2     (0x29U)
#define MCP2515_REG_CNF1     (0x2AU)
#define MCP2515_REG_CANINTE  (0x2BU)
#define MCP2515_REG_CANINTF  (0x2CU)
#define MCP2515_REG_EFLG     (0x2DU)
#define MCP2515_REG_TXB0CTRL (0x30U)
#define MCP2515_REG_TXB0SIDH (0x31U)
#define MCP2515_REG_RXB0CTRL (0x60U)
#define MCP2515_REG_RXB0SIDH (0x61U)
#define MCP2515_REG_RXB1CTRL (0x70U)
#define MCP2515_REG_RXB1SIDH (0x71U)

/* Bits. */
#define MCP2515_CANCTRL_REQOP_MASK (0xE0U)
#define MCP2515_MODE_NORMAL        (0x00U)
#define MCP2515_MODE_LOOPBACK      (0x40U)
#define MCP2515_MODE_CONFIG        (0x80U)
#define MCP2515_CANINT_RX0I        (0x01U)
#define MCP2515_CANINT_RX1I        (0x02U)
#define MCP2515_CANINT_TX0I        (0x04U)
#define MCP2515_CANINT_ERRI        (0x20U)
#define MCP2515_TXBCTRL_TXREQ      (0x08U)
#define MCP2515_RXBCTRL_ANY        (0x60U) /**< RXM = 11: receive any message, filters off. */
#define MCP2515_RXB0CTRL_BUKT      (0x04U) /**< Roll RXB0 over into RXB1 when full. */
#define MCP2515_EFLG_RX0OVR        (0x40U)
#define MCP2515_EFLG_RX1OVR        (0x80U)
#define MCP2515_EFLG_TXBO          (0x20U)

/**
 * Full-duplex SPI transfer of @p len bytes with chip select asserted from the first to the last
 * byte. @p rx may be NULL. Returns 0 on success, non-zero on failure.
 */
typedef int (*satlink_mcp2515_xfer_fn)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);

/** Bit timing register values. */
typedef struct
{
    uint8_t cnf1;
    uint8_t cnf2;
    uint8_t cnf3;
} satlink_mcp2515_timing_t;

/** Operating mode after init. */
typedef enum
{
    SATLINK_MCP2515_NORMAL = 0,
    SATLINK_MCP2515_LOOPBACK = 1, /**< Internal loopback: no bus needed (self test). */
} satlink_mcp2515_mode_t;

/** Driver instance. */
typedef struct
{
    satlink_mcp2515_xfer_fn xfer;
    void *ctx;
    uint32_t rx_overruns; /**< Frames the chip dropped because both RX buffers were full. */
} satlink_mcp2515_t;

/**
 * @brief Bit timing for @p bitrate from an oscillator of @p fosc_hz.
 *
 * Picks the largest number of time quanta between 8 and 25 (preferring 16) that divides evenly,
 * with the sample point at about 75 % and SJW = 1. Returns SATLINK_ERR_RANGE if no setting
 * gives the exact bit rate.
 */
satlink_status_t satlink_mcp2515_bit_timing(uint32_t fosc_hz, uint32_t bitrate,
                                            satlink_mcp2515_timing_t *timing);

/**
 * @brief Reset the chip, program the bit timing, accept all standard frames (RXB0 rolling over
 *        into RXB1), enable RX and error interrupts and enter @p mode.
 *
 * Returns SATLINK_ERR_IO if a transfer fails and SATLINK_ERR_TIMEOUT if the chip does not
 * report the requested mode.
 */
satlink_status_t satlink_mcp2515_init(satlink_mcp2515_t *dev, satlink_mcp2515_xfer_fn xfer,
                                      void *ctx, const satlink_mcp2515_timing_t *timing,
                                      satlink_mcp2515_mode_t mode);

/**
 * @brief Queue @p frame in TXB0 and request transmission.
 *
 * Returns SATLINK_ERR_FULL while the previous frame is still pending, SATLINK_ERR_RANGE for an
 * extended ID or DLC > 8.
 */
satlink_status_t satlink_mcp2515_send(satlink_mcp2515_t *dev, const satlink_can_frame_t *frame);

/**
 * @brief Fetch the oldest received frame.
 *
 * Returns SATLINK_ERR_EMPTY when no frame is waiting. Counts and clears RX overruns.
 */
satlink_status_t satlink_mcp2515_receive(satlink_mcp2515_t *dev, satlink_can_frame_t *frame);

/** Read one register. */
satlink_status_t satlink_mcp2515_read_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t *value);

/** Write one register. */
satlink_status_t satlink_mcp2515_write_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t value);

/** Change the bits in @p mask of @p reg to @p value (BIT MODIFY instruction). */
satlink_status_t satlink_mcp2515_modify_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t mask,
                                            uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_DRIVERS_MCP2515_H */
