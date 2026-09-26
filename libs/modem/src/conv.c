/**
 * @file conv.c
 * @brief K = 7 convolutional encoder, CCSDS puncturing and a soft-decision Viterbi decoder.
 *
 * The decoder keeps one decision bit per state and trellis step (64 bits per step) and traces
 * back once at the end of the terminated block: 8 bytes x (2048 + 6) steps = 16 KiB of static
 * memory, no heap.
 *
 * @implements SRS-MDM-001
 */
#include "satlink/modem/conv.h"

#include <limits.h>

#define G1        (0x79U) /* 171 octal */
#define G2        (0x5BU) /* 133 octal */
#define MAX_STEPS (SATLINK_CONV_MAX_INFO_BITS + SATLINK_CONV_TAIL_BITS)

/* Puncturing patterns (CCSDS 131.0-B table 3-2): per period, which C1 / C2 bits are sent. */
typedef struct
{
    uint8_t period;
    uint8_t c1[7];
    uint8_t c2[7];
} puncture_t;

static const puncture_t k_puncture[SATLINK_CONV_RATE_COUNT] = {
    {1U, {1U}, {1U}},
    {2U, {1U, 0U}, {1U, 1U}},
    {3U, {1U, 0U, 1U}, {1U, 1U, 0U}},
    {5U, {1U, 0U, 1U, 0U, 1U}, {1U, 1U, 0U, 1U, 0U}},
    {7U, {1U, 0U, 0U, 0U, 1U, 0U, 1U}, {1U, 1U, 1U, 1U, 0U, 1U, 0U}},
};

static uint8_t parity7(uint32_t v)
{
    uint32_t x = v & 0x7FU;
    x ^= x >> 4U;
    x ^= x >> 2U;
    x ^= x >> 1U;
    return (uint8_t)(x & 1U);
}

/* Outputs for input bit @p bit entering a register whose previous six bits are @p state
 * (state bit 5 = most recent). Register = bit<<6 | state. */
static void branch_outputs(uint32_t state, uint32_t bit, uint8_t *c1, uint8_t *c2)
{
    const uint32_t reg = (bit << 6U) | state;
    *c1 = parity7(reg & G1);
    *c2 = (uint8_t)(parity7(reg & G2) ^ 1U); /* CCSDS: C2 inverted */
}

size_t satlink_conv_coded_bits(size_t info_bits, satlink_conv_rate_t rate)
{
    if ((uint32_t)rate >= (uint32_t)SATLINK_CONV_RATE_COUNT)
    {
        return 0U;
    }
    const puncture_t *p = &k_puncture[rate];
    const size_t steps = info_bits + SATLINK_CONV_TAIL_BITS;
    size_t count = 0U;
    for (size_t i = 0U; i < steps; ++i)
    {
        const uint32_t phase = (uint32_t)(i % p->period);
        count += (size_t)p->c1[phase] + (size_t)p->c2[phase];
    }
    return count;
}

satlink_status_t satlink_conv_encode(const uint8_t *in, size_t info_bits, satlink_conv_rate_t rate,
                                     uint8_t *out, size_t out_len)
{
    if ((out == NULL) || ((in == NULL) && (info_bits > 0U)))
    {
        return SATLINK_ERR_NULL;
    }
    const size_t needed = satlink_conv_coded_bits(info_bits, rate);
    if ((needed == 0U) || (needed > out_len))
    {
        return SATLINK_ERR_RANGE;
    }
    const puncture_t *p = &k_puncture[rate];
    uint32_t state = 0U;
    size_t n = 0U;
    const size_t steps = info_bits + SATLINK_CONV_TAIL_BITS;
    for (size_t i = 0U; i < steps; ++i)
    {
        uint32_t bit = 0U;
        if (i < info_bits)
        {
            bit = ((uint32_t)in[i / 8U] >> (7U - (uint32_t)(i % 8U))) & 1U;
        }
        uint8_t c1 = 0U;
        uint8_t c2 = 0U;
        branch_outputs(state, bit, &c1, &c2);
        const uint32_t phase = (uint32_t)(i % p->period);
        if (p->c1[phase] != 0U)
        {
            out[n] = c1;
            ++n;
        }
        if (p->c2[phase] != 0U)
        {
            out[n] = c2;
            ++n;
        }
        state = ((state >> 1U) | (bit << 5U)) & 0x3FU;
    }
    return SATLINK_OK;
}

/* Decision bits: bit s of word [step] set = state s was reached from the predecessor with
 * the oldest register bit = 1. */
static uint64_t g_decisions[MAX_STEPS];

satlink_status_t satlink_conv_decode(const int8_t *soft, size_t soft_len, size_t info_bits,
                                     satlink_conv_rate_t rate, uint8_t *out, uint32_t *metric)
{
    if ((soft == NULL) || (out == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    const size_t needed = satlink_conv_coded_bits(info_bits, rate);
    if ((needed == 0U) || (soft_len < needed) || (info_bits > SATLINK_CONV_MAX_INFO_BITS))
    {
        return SATLINK_ERR_RANGE;
    }

    /* Path metrics: larger = better (correlation metric). */
    int32_t pm[SATLINK_CONV_STATES];
    int32_t next[SATLINK_CONV_STATES];
    for (uint32_t s = 0U; s < SATLINK_CONV_STATES; ++s)
    {
        pm[s] = (s == 0U) ? 0 : (INT32_MIN / 4);
    }

    const puncture_t *p = &k_puncture[rate];
    const size_t steps = info_bits + SATLINK_CONV_TAIL_BITS;
    size_t n = 0U;
    for (size_t i = 0U; i < steps; ++i)
    {
        const uint32_t phase = (uint32_t)(i % p->period);
        int32_t s1 = 0;
        int32_t s2 = 0;
        if (p->c1[phase] != 0U)
        {
            s1 = (int32_t)soft[n];
            ++n;
        }
        if (p->c2[phase] != 0U)
        {
            s2 = (int32_t)soft[n];
            ++n;
        }

        uint64_t decisions = 0U;
        for (uint32_t ns = 0U; ns < SATLINK_CONV_STATES; ++ns)
        {
            /* New state ns = (old >> 1) | (bit << 5): bit = ns bit 5; predecessors differ in the
             * bit shifted out (old bit 0). */
            const uint32_t bit = ns >> 5U;
            const uint32_t base = (ns << 1U) & 0x3FU;
            int32_t best = INT32_MIN;
            uint32_t choice = 0U;
            for (uint32_t oldest = 0U; oldest < 2U; ++oldest)
            {
                const uint32_t prev = base | oldest;
                uint8_t c1 = 0U;
                uint8_t c2 = 0U;
                branch_outputs(prev, bit, &c1, &c2);
                /* soft > 0 favours 0: add +soft when the branch bit is 0, -soft when 1 */
                const int32_t bm = ((c1 == 0U) ? s1 : -s1) + ((c2 == 0U) ? s2 : -s2);
                const int32_t m = pm[prev] + bm;
                if (m > best)
                {
                    best = m;
                    choice = oldest;
                }
            }
            next[ns] = best;
            decisions |= (uint64_t)choice << ns;
        }
        g_decisions[i] = decisions;
        /* Renormalise to keep the metrics away from overflow. */
        int32_t top = INT32_MIN;
        for (uint32_t s = 0U; s < SATLINK_CONV_STATES; ++s)
        {
            if (next[s] > top)
            {
                top = next[s];
            }
        }
        for (uint32_t s = 0U; s < SATLINK_CONV_STATES; ++s)
        {
            pm[s] = (next[s] < (INT32_MIN / 2)) ? (INT32_MIN / 4) : (next[s] - top);
        }
    }

    if (metric != NULL)
    {
        int32_t second = INT32_MIN;
        for (uint32_t s = 1U; s < SATLINK_CONV_STATES; ++s)
        {
            if (pm[s] > second)
            {
                second = pm[s];
            }
        }
        *metric = (uint32_t)(pm[0] - second);
    }

    /* Trace back from state 0 (terminated). */
    const size_t out_bytes = (info_bits + 7U) / 8U;
    for (size_t i = 0U; i < out_bytes; ++i)
    {
        out[i] = 0U;
    }
    uint32_t state = 0U;
    for (size_t i = steps; i > 0U; --i)
    {
        const size_t step = i - 1U;
        const uint32_t bit = state >> 5U;
        const uint32_t oldest = (uint32_t)((g_decisions[step] >> state) & 1U);
        if ((step < info_bits) && (bit != 0U))
        {
            out[step / 8U] |= (uint8_t)(0x80U >> (uint32_t)(step % 8U));
        }
        state = ((state << 1U) & 0x3FU) | oldest;
    }
    return SATLINK_OK;
}
