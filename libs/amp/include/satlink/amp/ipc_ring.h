/**
 * @file ipc_ring.h
 * @brief Lock-free single-producer / single-consumer message ring in shared memory, used between
 *        Linux (Core 0) and FreeRTOS (Core 1).
 *
 * The same source is compiled into the Linux kernel module (satlink_amp), the FreeRTOS firmware
 * and the host tests, so it depends on nothing but fixed-width integers and a barrier hook.
 *
 * Layout in shared memory: a 128-byte header (producer index and consumer index in separate
 * 64-byte cache lines) followed by the data area (a power of two in bytes). Indices count bytes
 * and wrap at 2^32; a record never straddles the end of the data area: when it would, the
 * producer writes a padding marker and starts again at offset 0.
 *
 * Record: [u16 length][u16 type][u32 sequence][payload, padded to a multiple of 4]. All fields
 * little-endian (both cores are little-endian Cortex-A9).
 *
 * Both sides map the shared region non-cacheable, so ordering is the only concern: the
 * producer writes the record, then a barrier, then publishes the head; the consumer reads the
 * head, a barrier, the record, a barrier, then publishes the tail.
 *
 * @implements SRS-AMP-002
 */
#ifndef SATLINK_AMP_IPC_RING_H
#define SATLINK_AMP_IPC_RING_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SATLINK_RING_MAGIC       (0x534C5247UL) /* "SLRG" */
#define SATLINK_RING_HDR_BYTES   (128U)
#define SATLINK_RING_REC_HDR     (8U)
#define SATLINK_RING_MAX_PAYLOAD (512U)
#define SATLINK_RING_PAD_TYPE    (0xFFFFU)

/** Result codes (kept separate from satlink_status_t so the kernel build needs no libs). */
#define SATLINK_RING_OK      (0)
#define SATLINK_RING_EMPTY   (-1)
#define SATLINK_RING_FULL    (-2)
#define SATLINK_RING_TOO_BIG (-3) /* payload > SATLINK_RING_MAX_PAYLOAD or > caller buffer */
#define SATLINK_RING_CORRUPT (-4) /* header magic/size wrong or a record is malformed */

/** Shared header. Only the owner writes each index. */
typedef struct
{
    volatile uint32_t head; /**< Producer: bytes written. */
    uint32_t magic;
    uint32_t size; /**< Data area bytes (power of two). */
    uint32_t reserved0[13];
    volatile uint32_t tail; /**< Consumer: bytes consumed. */
    uint32_t reserved1[15];
} satlink_ring_hdr_t;

/** Local handle (not shared). */
typedef struct
{
    satlink_ring_hdr_t *hdr;
    uint8_t *data;
    uint32_t size;
    uint32_t seq; /**< Producer: next sequence number. */
} satlink_ring_t;

/**
 * @brief Initialise a ring in @p mem (@p bytes total, header included) and attach to it.
 * Called once by the side that owns the memory layout (Linux, before starting Core 1).
 * The data area is the largest power of two that fits.
 */
int satlink_ring_init(satlink_ring_t *ring, void *mem, uint32_t bytes);

/** Attach to a ring another side initialised. Checks magic and size. */
int satlink_ring_attach(satlink_ring_t *ring, void *mem, uint32_t bytes);

/** Queue one message. Returns SATLINK_RING_FULL if there is not enough room. */
int satlink_ring_write(satlink_ring_t *ring, uint16_t type, const void *payload, uint16_t len);

/**
 * @brief Take one message.
 * @param len  In: capacity of @p buf. Out: payload length.
 * @return SATLINK_RING_EMPTY if nothing is queued, SATLINK_RING_TOO_BIG (message left in the
 *         ring, @p len set to the size needed) if @p buf is too small.
 */
int satlink_ring_read(satlink_ring_t *ring, uint16_t *type, uint32_t *seq, void *buf,
                      uint16_t *len);

/** Bytes currently queued. */
uint32_t satlink_ring_used(const satlink_ring_t *ring);

/** Free bytes (a message needs 8 + padded payload, possibly plus wrap padding). */
uint32_t satlink_ring_free(const satlink_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* SATLINK_AMP_IPC_RING_H */
