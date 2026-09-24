/**
 * @file mcp2515.h
 * @brief MCP2515 stand-alone CAN controller driver over an abstract SPI transfer.
 *
 * The driver only builds SPI transactions; the caller supplies the transfer function (AXI Quad
 * SPI on the MicroBlaze V, a simulator in the unit tests). It uses no dynamic memory and no
 * operating system services, and works in polling mode: no interrupt line is required.
 *
 * @implements SRS-HKC-002
 */
#ifndef SATLINK_CAN_MCP2515_H
#define SATLINK_CAN_MCP2515_H

#include <stddef.h>
#include <stdint.h>

#include "satlink/can/can_frame.h"
#include "satlink/common/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One SPI transaction: chip select is asserted for the whole call, @p len bytes are clocked out
 * from @p tx while @p len bytes are captured into @p rx.
 * @return ::SATLINK_OK, or an error code (usually ::SATLINK_ERR_IO).
 */
typedef satlink_status_t (*satlink_mcp2515_transfer_fn)(void *ctx, const uint8_t *tx, uint8_t *rx,
                                                        size_t len);

/** Optional busy-wait, used while the controller resets and changes mode. May be NULL. */
typedef void (*satlink_mcp2515_delay_fn)(void *ctx, uint32_t microseconds);

/** Operating mode (CANCTRL.REQOP). */
typedef enum
{
    SATLINK_MCP2515_MODE_NORMAL = 0,
    SATLINK_MCP2515_MODE_SLEEP = 1,
    SATLINK_MCP2515_MODE_LOOPBACK = 2,
    SATLINK_MCP2515_MODE_LISTEN_ONLY = 3,
    SATLINK_MCP2515_MODE_CONFIG = 4
} satlink_mcp2515_mode_t;

/** Bit timing registers CNF1..CNF3. */
typedef struct
{
    uint8_t cnf1;
    uint8_t cnf2;
    uint8_t cnf3;
} satlink_mcp2515_timing_t;

/** Driver instance. Fill with satlink_mcp2515_init(). */
typedef struct
{
    satlink_mcp2515_transfer_fn transfer;
    satlink_mcp2515_delay_fn delay_us;
    void *ctx;
} satlink_mcp2515_t;

/** EFLG bits, as returned by satlink_mcp2515_error_flags(). */
#define SATLINK_MCP2515_EFLG_EWARN  (0x01U)
#define SATLINK_MCP2515_EFLG_RXWAR  (0x02U)
#define SATLINK_MCP2515_EFLG_TXWAR  (0x04U)
#define SATLINK_MCP2515_EFLG_RXEP   (0x08U)
#define SATLINK_MCP2515_EFLG_TXEP   (0x10U)
#define SATLINK_MCP2515_EFLG_TXBO   (0x20U)
#define SATLINK_MCP2515_EFLG_RX0OVR (0x40U)
#define SATLINK_MCP2515_EFLG_RX1OVR (0x80U)

/**
 * @brief Compute CNF1..CNF3 for a bit rate, with a sample point near 87.5 %.
 *
 * Picks the number of time quanta per bit (16 preferred, then 15..8, then 17..25) for which the
 * oscillator divides evenly. The synchronization jump width is one time quantum.
 *
 * @param osc_hz   Oscillator frequency in hertz.
 * @param bitrate  Bit rate in bit/s.
 * @param timing   Result.
 * @return ::SATLINK_OK, ::SATLINK_ERR_NULL, or ::SATLINK_ERR_RANGE if no valid setting exists.
 */
satlink_status_t satlink_mcp2515_calc_timing(uint32_t osc_hz, uint32_t bitrate,
                                             satlink_mcp2515_timing_t *timing);

/** Store the transfer callback; does not touch the hardware. @p delay_us may be NULL. */
satlink_status_t satlink_mcp2515_init(satlink_mcp2515_t *dev, satlink_mcp2515_transfer_fn transfer,
                                      satlink_mcp2515_delay_fn delay_us, void *ctx);

/**
 * @brief Reset the controller, program the bit timing, accept all frames and enter @p mode.
 *
 * Receive filters are switched off (RXM = any) and roll-over from buffer 0 to buffer 1 is on;
 * interrupts are left disabled because the driver polls.
 */
satlink_status_t satlink_mcp2515_configure(const satlink_mcp2515_t *dev, uint32_t osc_hz,
                                           uint32_t bitrate, satlink_mcp2515_mode_t mode);

/** Request a mode and wait until CANSTAT reports it. ::SATLINK_ERR_TIMEOUT if it never does. */
satlink_status_t satlink_mcp2515_set_mode(const satlink_mcp2515_t *dev,
                                          satlink_mcp2515_mode_t mode);

/**
 * @brief Queue one frame in transmit buffer 0.
 * @return ::SATLINK_ERR_BUSY if the buffer still holds the previous frame.
 */
satlink_status_t satlink_mcp2515_send(const satlink_mcp2515_t *dev,
                                      const satlink_can_frame_t *frame);

/** Fetch one received frame, or ::SATLINK_ERR_EMPTY if both receive buffers are empty. */
satlink_status_t satlink_mcp2515_receive(const satlink_mcp2515_t *dev, satlink_can_frame_t *frame);

/** Read the error flag register (see SATLINK_MCP2515_EFLG_*). */
satlink_status_t satlink_mcp2515_error_flags(const satlink_mcp2515_t *dev, uint8_t *flags);

/** Clear the receive overflow flags (RX0OVR, RX1OVR). */
satlink_status_t satlink_mcp2515_clear_overflow(const satlink_mcp2515_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_CAN_MCP2515_H */
