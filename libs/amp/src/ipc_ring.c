/**
 * @file ipc_ring.c
 * @implements SRS-AMP-002
 */
#include "satlink/amp/ipc_ring.h"

#ifdef __KERNEL__
#include <asm/barrier.h>
#include <linux/stddef.h>
#define RING_BARRIER() mb()
#elif defined(__arm__) && !defined(__linux__)
#define RING_BARRIER() __asm__ volatile("dmb" ::: "memory")
#else
#define RING_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

/* Byte copies to and from shared memory (no library call, same code in kernel and firmware). */
static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0U; i < n; ++i)
    {
        dst[i] = src[i];
    }
}

static uint32_t align4(uint32_t n)
{
    return (n + 3U) & ~3U;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8U);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)(v >> 24U);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint32_t data_size_for(uint32_t bytes)
{
    if (bytes <= (SATLINK_RING_HDR_BYTES + 64U))
    {
        return 0U;
    }
    const uint32_t avail = bytes - SATLINK_RING_HDR_BYTES;
    uint32_t size = 64U;
    while ((size * 2U) <= avail)
    {
        size *= 2U;
    }
    return size;
}

int satlink_ring_init(satlink_ring_t *ring, void *mem, uint32_t bytes)
{
    const uint32_t size = data_size_for(bytes);
    if ((ring == NULL) || (mem == NULL) || (size == 0U))
    {
        return SATLINK_RING_CORRUPT;
    }
    satlink_ring_hdr_t *hdr = (satlink_ring_hdr_t *)mem;
    hdr->head = 0U;
    hdr->tail = 0U;
    hdr->size = size;
    RING_BARRIER();
    hdr->magic = SATLINK_RING_MAGIC;
    RING_BARRIER();
    return satlink_ring_attach(ring, mem, bytes);
}

int satlink_ring_attach(satlink_ring_t *ring, void *mem, uint32_t bytes)
{
    if ((ring == NULL) || (mem == NULL))
    {
        return SATLINK_RING_CORRUPT;
    }
    satlink_ring_hdr_t *hdr = (satlink_ring_hdr_t *)mem;
    const uint32_t size = data_size_for(bytes);
    if ((hdr->magic != SATLINK_RING_MAGIC) || (hdr->size != size) || (size == 0U))
    {
        return SATLINK_RING_CORRUPT;
    }
    ring->hdr = hdr;
    ring->data = (uint8_t *)mem + SATLINK_RING_HDR_BYTES;
    ring->size = size;
    ring->seq = 0U;
    return SATLINK_RING_OK;
}

uint32_t satlink_ring_used(const satlink_ring_t *ring)
{
    return ring->hdr->head - ring->hdr->tail;
}

uint32_t satlink_ring_free(const satlink_ring_t *ring)
{
    return ring->size - satlink_ring_used(ring);
}

int satlink_ring_write(satlink_ring_t *ring, uint16_t type, const void *payload, uint16_t len)
{
    if ((ring == NULL) || ((payload == NULL) && (len > 0U)) || (type == SATLINK_RING_PAD_TYPE))
    {
        return SATLINK_RING_CORRUPT;
    }
    if (len > SATLINK_RING_MAX_PAYLOAD)
    {
        return SATLINK_RING_TOO_BIG;
    }
    const uint32_t record = SATLINK_RING_REC_HDR + align4(len);
    const uint32_t head = ring->hdr->head;
    const uint32_t offset = head & (ring->size - 1U);
    const uint32_t to_end = ring->size - offset;
    const uint32_t pad = (record > to_end) ? to_end : 0U;
    if ((pad + record) > satlink_ring_free(ring))
    {
        return SATLINK_RING_FULL;
    }

    uint32_t pos = head;
    if (pad != 0U)
    {
        /* The record would straddle the end: mark the rest of the area as padding. */
        uint8_t *p = &ring->data[offset];
        if (to_end >= 4U)
        {
            put16(p, 0U);
            put16(&p[2], (uint16_t)SATLINK_RING_PAD_TYPE);
        }
        pos += pad;
    }
    uint8_t *rec = &ring->data[pos & (ring->size - 1U)];
    put16(rec, len);
    put16(&rec[2], type);
    put32(&rec[4], ring->seq);
    if (len > 0U)
    {
        copy_bytes(&rec[SATLINK_RING_REC_HDR], (const uint8_t *)payload, len);
    }
    ring->seq++;
    RING_BARRIER();
    ring->hdr->head = pos + record;
    return SATLINK_RING_OK;
}

int satlink_ring_read(satlink_ring_t *ring, uint16_t *type, uint32_t *seq, void *buf, uint16_t *len)
{
    if ((ring == NULL) || (type == NULL) || (len == NULL) || ((buf == NULL) && (*len > 0U)))
    {
        return SATLINK_RING_CORRUPT;
    }
    for (;;)
    {
        const uint32_t head = ring->hdr->head;
        RING_BARRIER();
        uint32_t tail = ring->hdr->tail;
        if (head == tail)
        {
            return SATLINK_RING_EMPTY;
        }
        if ((head - tail) > ring->size)
        {
            return SATLINK_RING_CORRUPT;
        }
        const uint32_t offset = tail & (ring->size - 1U);
        const uint32_t to_end = ring->size - offset;
        const uint8_t *rec = &ring->data[offset];
        if ((to_end < SATLINK_RING_REC_HDR) || (get16(&rec[2]) == SATLINK_RING_PAD_TYPE))
        {
            /* Padding up to the end of the data area: skip it. */
            tail += to_end;
            RING_BARRIER();
            ring->hdr->tail = tail;
            continue;
        }
        const uint16_t n = get16(rec);
        const uint32_t record = SATLINK_RING_REC_HDR + align4(n);
        if ((n > SATLINK_RING_MAX_PAYLOAD) || (record > to_end) || (record > (head - tail)))
        {
            return SATLINK_RING_CORRUPT;
        }
        if (n > *len)
        {
            *len = n;
            return SATLINK_RING_TOO_BIG;
        }
        *type = get16(&rec[2]);
        if (seq != NULL)
        {
            *seq = get32(&rec[4]);
        }
        if (n > 0U)
        {
            copy_bytes((uint8_t *)buf, &rec[SATLINK_RING_REC_HDR], n);
        }
        *len = n;
        RING_BARRIER();
        ring->hdr->tail = tail + record;
        return SATLINK_RING_OK;
    }
}
