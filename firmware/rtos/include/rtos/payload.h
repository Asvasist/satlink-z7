/**
 * @file payload.h
 * @brief PL modem datapath: payload_ctrl registers and the axi_dma_modem streams.
 *
 * Stream format (both directions): one 32-bit word per complex value, I in bits 15:0, Q in bits
 * 31:16, signed Q15, unit amplitude = 16384 (-6 dBFS, headroom for the RRC overshoot).
 * TX: symbols at 6 kSym/s (the PL interpolates by 8 with the RRC and mixes to 12 kHz).
 * RX: matched-filtered baseband decimated to 12 kS/s (2 samples per symbol), RX_FRAME_LEN
 *     (1024) samples per DMA packet.
 *
 * @implements SRS-AMP-006
 */
#ifndef RTOS_PAYLOAD_H
#define RTOS_PAYLOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "satlink/modem/types.h"

#include "FreeRTOS.h"
#include "task.h"

#define PAYLOAD_TX_SYMBOLS_PER_BUF (256U)
#define PAYLOAD_TX_BUFS            (8U)
#define PAYLOAD_RX_SAMPLES_PER_BUF (1024U)
#define PAYLOAD_RX_BUFS            (16U)
#define PAYLOAD_SAMPLE_SCALE       (16384.0F)

/** Check the block version and reset the DMA. False if the PL design is not the expected one. */
bool payload_probe(void);

/** Start both streams; completed buffers notify @p task with @p tx_bit / @p rx_bit. */
void payload_start(TaskHandle_t task, uint32_t tx_bit, uint32_t rx_bit);

/** Stop the streams and mute the codec. */
void payload_stop(void);

/** Refill every TX buffer the engine has finished, using @p fill for the symbols. */
void payload_service_tx(void (*fill)(void *ctx, satlink_cf_t *symbols, uint32_t count), void *ctx);

/** Hand every received buffer to @p sink (float samples), then re-arm it. */
void payload_service_rx(void (*sink)(void *ctx, const satlink_cf_t *samples, uint32_t count),
                        void *ctx);

void payload_set_channel(uint16_t noise_level, uint16_t gain_q15);
void payload_set_digital_loopback(bool on);

/** Fault counters: TX underruns (PL + DMA idle) and RX overruns. */
uint32_t payload_underruns(void);
uint32_t payload_overruns(void);

/** Q15 packing helpers (host-testable, pure). */
uint32_t payload_pack(satlink_cf_t v);
satlink_cf_t payload_unpack(uint32_t word);

#endif /* RTOS_PAYLOAD_H */
