/**
 * @file test_can_boot.c
 * @brief CAN upload protocol: receiver rules, frame layouts and whole transfers over a simulated
 *        bus that drops, duplicates and corrupts frames.
 *
 * @verifies SRS-HKC-003
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/boot/can_boot.h"
#include "satlink/common/crc16_ccitt.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void fill_bytes(void *dst, uint8_t value, size_t len)
{
    uint8_t *p = (uint8_t *)dst;

    for (size_t i = 0U; i < len; ++i)
    {
        p[i] = value;
    }
}

static void copy_bytes(void *dst, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    for (size_t i = 0U; i < len; ++i)
    {
        d[i] = s[i];
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Test fixtures                                                                              */
/* ------------------------------------------------------------------------------------------ */

typedef struct
{
    uint8_t mem[2048];
    uint32_t fail_at_offset; /* fail the write that starts here; UINT32_MAX = never */
    unsigned writes;
} sink_t;

static satlink_status_t sink_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    sink_t *sink = (sink_t *)ctx;
    satlink_status_t status = SATLINK_OK;

    sink->writes++;
    if (offset == sink->fail_at_offset)
    {
        status = SATLINK_ERR_IO;
    }
    else if (((size_t)offset + len) > sizeof(sink->mem))
    {
        status = SATLINK_ERR_RANGE;
    }
    else
    {
        copy_bytes(&sink->mem[offset], data, len);
    }

    return status;
}

static void make_rx(satlink_canboot_rx_t *rx, sink_t *sink, uint32_t max_size, uint16_t window)
{
    const satlink_canboot_rx_config_t cfg = {
        .write = sink_write, .ctx = sink, .max_size = max_size, .window = window};

    fill_bytes(sink, 0, sizeof(*sink));
    sink->fail_at_offset = UINT32_MAX;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_init(rx, &cfg));
}

static void make_image(uint8_t *image, uint32_t size)
{
    for (uint32_t i = 0U; i < size; ++i)
    {
        image[i] = (uint8_t)((i * 7U) + 3U);
    }
}

static satlink_can_frame_t cmd(uint8_t dlc, const uint8_t *bytes)
{
    satlink_can_frame_t frame = {.id = SATLINK_CANBOOT_ID_CMD, .dlc = dlc};

    for (uint8_t i = 0U; i < dlc; ++i)
    {
        frame.data[i] = bytes[i];
    }
    return frame;
}

/** Feed one command to the receiver; returns the response (kind 0 if there was none). */
static satlink_can_frame_t send(satlink_canboot_rx_t *rx, uint8_t dlc, const uint8_t *bytes)
{
    const satlink_can_frame_t in = cmd(dlc, bytes);
    satlink_can_frame_t rsp;
    bool have = false;

    fill_bytes(&rsp, 0, sizeof(rsp));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(rx, &in, &rsp, &have));
    if (!have)
    {
        fill_bytes(&rsp, 0, sizeof(rsp));
    }
    return rsp;
}

static void begin(satlink_canboot_rx_t *rx, uint32_t size, uint16_t crc)
{
    const uint8_t bytes[7] = {0x02U,
                              (uint8_t)(size & 0xFFU),
                              (uint8_t)((size >> 8U) & 0xFFU),
                              (uint8_t)((size >> 16U) & 0xFFU),
                              (uint8_t)((size >> 24U) & 0xFFU),
                              (uint8_t)(crc & 0xFFU),
                              (uint8_t)(crc >> 8U)};
    const satlink_can_frame_t rsp = send(rx, 7U, bytes);

    TEST_ASSERT_EQUAL_HEX8(0x40U, rsp.data[0]);
}

static void expect_nak(const satlink_can_frame_t *rsp, uint8_t op, uint16_t err, uint32_t aux)
{
    TEST_ASSERT_EQUAL_HEX32(SATLINK_CANBOOT_ID_RSP, rsp->id);
    TEST_ASSERT_EQUAL_UINT8(8U, rsp->dlc);
    TEST_ASSERT_EQUAL_HEX8(0x41U, rsp->data[0]);
    TEST_ASSERT_EQUAL_HEX8(op, rsp->data[1]);
    TEST_ASSERT_EQUAL_HEX16(err, (uint16_t)(rsp->data[2] | ((uint16_t)rsp->data[3] << 8U)));
    TEST_ASSERT_EQUAL_HEX32(aux, (uint32_t)rsp->data[4] | ((uint32_t)rsp->data[5] << 8U) |
                                     ((uint32_t)rsp->data[6] << 16U) |
                                     ((uint32_t)rsp->data[7] << 24U));
}

static void expect_ack(const satlink_can_frame_t *rsp, uint8_t op, uint16_t value)
{
    TEST_ASSERT_EQUAL_HEX32(SATLINK_CANBOOT_ID_RSP, rsp->id);
    TEST_ASSERT_EQUAL_HEX8(0x40U, rsp->data[0]);
    TEST_ASSERT_EQUAL_HEX8(op, rsp->data[1]);
    TEST_ASSERT_EQUAL_HEX16(value, (uint16_t)(rsp->data[2] | ((uint16_t)rsp->data[3] << 8U)));
}

/* ------------------------------------------------------------------------------------------ */
/* A bus between sender and receiver                                                          */
/* ------------------------------------------------------------------------------------------ */

typedef struct
{
    uint32_t seed;
    unsigned drop_percent;
    bool duplicate;
    unsigned
        corrupt_data_frame; /* corrupt the payload of the n-th DATA frame delivered; 0 = none */
    unsigned data_frames_seen;
    unsigned frames_sent;
} channel_t;

static bool chan_drops(channel_t *ch)
{
    ch->seed = (ch->seed * 1664525U) + 1013904223U;
    return ((ch->seed >> 16U) % 100U) < ch->drop_percent;
}

static void deliver(satlink_canboot_tx_t *tx, satlink_canboot_rx_t *rx, channel_t *ch,
                    const satlink_can_frame_t *frame)
{
    const unsigned copies = ch->duplicate ? 2U : 1U;

    for (unsigned k = 0U; k < copies; ++k)
    {
        satlink_can_frame_t in = *frame;
        satlink_can_frame_t rsp;
        bool have = false;

        if (chan_drops(ch))
        {
            continue;
        }
        if ((in.data[0] == 0x03U) && (in.dlc > 3U))
        {
            ch->data_frames_seen++;
            if (ch->data_frames_seen == ch->corrupt_data_frame)
            {
                in.data[3] = (uint8_t)(in.data[3] ^ 0x01U);
            }
        }
        TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(rx, &in, &rsp, &have));
        if (have && !chan_drops(ch))
        {
            TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_on_response(tx, &rsp));
        }
    }
}

static satlink_canboot_tx_state_t run_transfer(satlink_canboot_tx_t *tx, satlink_canboot_rx_t *rx,
                                               channel_t *ch, unsigned max_rounds)
{
    for (unsigned round = 0U; round < max_rounds; ++round)
    {
        satlink_can_frame_t frame;

        while (satlink_canboot_tx_next(tx, &frame))
        {
            ch->frames_sent++;
            deliver(tx, rx, ch, &frame);
        }

        if ((satlink_canboot_tx_state(tx) == SATLINK_CANBOOT_TX_DONE) ||
            (satlink_canboot_tx_state(tx) == SATLINK_CANBOOT_TX_FAILED))
        {
            break;
        }
        satlink_canboot_tx_on_timeout(tx); /* nothing more to send and no result: time passes */
    }
    return satlink_canboot_tx_state(tx);
}

static void transfer_and_check(uint32_t size, uint16_t window, unsigned drop_percent,
                               bool duplicate, uint32_t seed)
{
    static uint8_t image[1500];
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_canboot_tx_t tx;
    channel_t ch = {.seed = seed, .drop_percent = drop_percent, .duplicate = duplicate};

    make_image(image, size);
    make_rx(&rx, &sink, 1500U, window);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_init(&tx, image, size, window, true, 60U));

    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_DONE, run_transfer(&tx, &rx, &ch, 20000U));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_VERIFIED, satlink_canboot_rx_state(&rx));
    TEST_ASSERT_TRUE(satlink_canboot_rx_boot_requested(&rx));
    TEST_ASSERT_EQUAL_UINT32(size, satlink_canboot_rx_image_size(&rx));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image, sink.mem, size);
    TEST_ASSERT_EQUAL_UINT8(100U, satlink_canboot_tx_progress(&tx));
}

/* ------------------------------------------------------------------------------------------ */
/* Tests                                                                                      */
/* ------------------------------------------------------------------------------------------ */

static void test_a_small_image_goes_over_in_the_expected_frames(void)
{
    static const uint8_t image[7] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U};
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_canboot_tx_t tx;
    channel_t ch = {.seed = 1U};

    make_rx(&rx, &sink, 1024U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_init(&tx, image, sizeof(image), 0U, true, 3U));

    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_DONE, run_transfer(&tx, &rx, &ch, 10U));
    TEST_ASSERT_EQUAL_UINT(5U, ch.frames_sent); /* BEGIN, 2 x DATA, END, BOOT */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image, sink.mem, sizeof(image));
    TEST_ASSERT_TRUE(satlink_canboot_rx_boot_requested(&rx));
}

static void test_frame_layouts_on_the_wire(void)
{
    static const uint8_t image[7] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U};
    satlink_canboot_tx_t tx;
    satlink_can_frame_t f;
    const uint16_t crc = satlink_crc16_ccitt(image, sizeof(image));

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_canboot_tx_init(&tx, image, sizeof(image), 0U, false, 3U));

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f));
    TEST_ASSERT_EQUAL_HEX32(0x7E0U, f.id);
    TEST_ASSERT_FALSE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(7U, f.dlc);
    {
        const uint8_t expected[7] = {
            0x02U, 0x07U, 0x00U, 0x00U, 0x00U, (uint8_t)(crc & 0xFFU), (uint8_t)(crc >> 8U)};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, f.data, 7U);
    }
    TEST_ASSERT_FALSE(satlink_canboot_tx_next(&tx, &f)); /* waits for the answer to BEGIN */

    {
        const uint8_t ack_begin[8] = {0x40U, 0x02U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U};
        satlink_can_frame_t rsp = {.id = 0x7E1U, .dlc = 8U};

        copy_bytes(rsp.data, ack_begin, 8U);
        TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_on_response(&tx, &rsp));
    }

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f));
    TEST_ASSERT_EQUAL_UINT8(8U, f.dlc);
    {
        const uint8_t expected[8] = {0x03U, 0x00U, 0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, f.data, 8U);
    }
    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f));
    TEST_ASSERT_EQUAL_UINT8(5U, f.dlc); /* the short last chunk */
    {
        const uint8_t expected[5] = {0x03U, 0x01U, 0x00U, 0x66U, 0x77U};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, f.data, 5U);
    }
}

static void test_images_of_every_awkward_size_and_window(void)
{
    static const uint32_t sizes[] = {1U, 4U, 5U, 6U, 10U, 11U, 64U, 1000U};
    static const uint16_t windows[] = {1U, 3U, 16U};

    for (size_t i = 0U; i < (sizeof(sizes) / sizeof(sizes[0])); ++i)
    {
        for (size_t j = 0U; j < (sizeof(windows) / sizeof(windows[0])); ++j)
        {
            transfer_and_check(sizes[i], windows[j], 0U, false, 7U);
        }
    }
}

static void test_a_lossy_bus_still_delivers_the_image(void)
{
    for (uint32_t seed = 1U; seed <= 6U; ++seed)
    {
        transfer_and_check(700U, 8U, 15U, false, seed * 2654435761U);
    }
}

static void test_a_very_lossy_bus_with_a_window_of_one(void)
{
    transfer_and_check(120U, 1U, 30U, false, 12345U);
}

static void test_duplicated_frames_are_harmless(void)
{
    transfer_and_check(333U, 4U, 0U, true, 5U);
    transfer_and_check(333U, 4U, 10U, true, 99U);
}

static void test_a_corrupted_payload_is_caught_by_the_crc_and_boot_is_refused(void)
{
    static uint8_t image[200];
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_canboot_tx_t tx;
    channel_t ch = {.seed = 1U, .corrupt_data_frame = 3U};
    static const uint8_t boot[1] = {0x05U};

    make_image(image, sizeof(image));
    make_rx(&rx, &sink, 1024U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_init(&tx, image, sizeof(image), 0U, true, 3U));

    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_FAILED, run_transfer(&tx, &rx, &ch, 100U));
    TEST_ASSERT_EQUAL_UINT16(SATLINK_CANBOOT_ERR_CRC, satlink_canboot_tx_error(&tx));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_ERROR, satlink_canboot_rx_state(&rx));
    TEST_ASSERT_FALSE(satlink_canboot_rx_boot_requested(&rx));

    {
        const satlink_can_frame_t rsp = send(&rx, 1U, boot);
        expect_nak(&rsp, 0x05U, SATLINK_CANBOOT_ERR_STATE, rx.next_seq);
    }
}

static void test_the_sender_gives_up_after_its_retries(void)
{
    static const uint8_t image[10] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    satlink_canboot_tx_t tx;
    satlink_can_frame_t f;

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_canboot_tx_init(&tx, image, sizeof(image), 0U, false, 2U));

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f)); /* BEGIN, never answered */
    for (unsigned attempt = 0U; attempt < 2U; ++attempt)
    {
        satlink_canboot_tx_on_timeout(&tx);
        TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_BEGIN, satlink_canboot_tx_state(&tx));
        TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f)); /* BEGIN again */
    }
    satlink_canboot_tx_on_timeout(&tx);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_FAILED, satlink_canboot_tx_state(&tx));
    TEST_ASSERT_EQUAL_UINT16(SATLINK_CANBOOT_ERR_TIMEOUT, satlink_canboot_tx_error(&tx));
    TEST_ASSERT_FALSE(satlink_canboot_tx_next(&tx, &f));
}

static void test_a_receiver_that_is_ahead_of_a_rewound_sender_is_followed(void)
{
    static uint8_t image[100];
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_canboot_tx_t tx;
    satlink_can_frame_t f;
    satlink_can_frame_t rsp;
    bool have = false;

    make_image(image, sizeof(image));
    make_rx(&rx, &sink, 1024U, 4U);
    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_canboot_tx_init(&tx, image, sizeof(image), 4U, false, 5U));

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f)); /* BEGIN */
    (void)satlink_canboot_rx_handle(&rx, &f, &rsp, &have);
    (void)satlink_canboot_tx_on_response(&tx, &rsp);

    for (unsigned i = 0U; i < 4U; ++i) /* first window reaches the receiver, its ACK is lost */
    {
        TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f));
        (void)satlink_canboot_rx_handle(&rx, &f, &rsp, &have);
    }
    TEST_ASSERT_FALSE(satlink_canboot_tx_next(&tx, &f));
    satlink_canboot_tx_on_timeout(&tx); /* rewinds to frame 0 */

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f)); /* frame 0 again: the receiver has it */
    (void)satlink_canboot_rx_handle(&rx, &f, &rsp, &have);
    TEST_ASSERT_TRUE(have);
    expect_ack(&rsp, 0x03U, 4U); /* "I am at 4" */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_on_response(&tx, &rsp));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_DATA, satlink_canboot_tx_state(&tx)); /* not an error */

    TEST_ASSERT_TRUE(satlink_canboot_tx_next(&tx, &f)); /* continues after frame 3, not from 1 */
    TEST_ASSERT_EQUAL_HEX8(0x04U, f.data[1]);
}

static void test_stale_and_impossible_acknowledgements(void)
{
    static uint8_t image[50];
    satlink_canboot_tx_t tx;
    satlink_can_frame_t rsp = {.id = 0x7E1U, .dlc = 8U};

    make_image(image, sizeof(image));
    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_canboot_tx_init(&tx, image, sizeof(image), 4U, false, 5U));
    rsp.data[0] = 0x40U;
    rsp.data[1] = 0x02U; /* ACK BEGIN */
    (void)satlink_canboot_tx_on_response(&tx, &rsp);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_DATA, satlink_canboot_tx_state(&tx));

    rsp.data[1] = 0x03U; /* ACK DATA claiming frame 11 of 10 */
    rsp.data[2] = 11U;
    (void)satlink_canboot_tx_on_response(&tx, &rsp);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_FAILED, satlink_canboot_tx_state(&tx));
    TEST_ASSERT_EQUAL_UINT16(SATLINK_CANBOOT_ERR_SEQ, satlink_canboot_tx_error(&tx));
}

static void test_the_sender_ignores_frames_that_are_not_responses(void)
{
    static const uint8_t image[3] = {1U, 2U, 3U};
    satlink_canboot_tx_t tx;
    satlink_can_frame_t other = {.id = 0x123U, .dlc = 8U};

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_canboot_tx_init(&tx, image, sizeof(image), 0U, false, 3U));
    other.data[0] = 0x41U; /* looks like a NAK, but not on the response identifier */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_on_response(&tx, &other));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_BEGIN, satlink_canboot_tx_state(&tx));

    other.id = 0x7E1U;
    other.dlc = 5U; /* wrong length */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_tx_on_response(&tx, &other));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_TX_BEGIN, satlink_canboot_tx_state(&tx));
}

static void test_sender_init_rejects_bad_arguments(void)
{
    static const uint8_t image[4] = {1U, 2U, 3U, 4U};
    satlink_canboot_tx_t tx;

    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_tx_init(NULL, image, 4U, 0U, false, 1U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_tx_init(&tx, NULL, 4U, 0U, false, 1U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_canboot_tx_init(&tx, image, 0U, 0U, false, 1U));
    TEST_ASSERT_EQUAL(
        SATLINK_ERR_RANGE,
        satlink_canboot_tx_init(&tx, image, SATLINK_CANBOOT_MAX_IMAGE + 1U, 0U, false, 1U));
}

static void test_receiver_init_rejects_bad_configuration(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_canboot_rx_config_t cfg = {.write = sink_write, .ctx = &sink, .max_size = 100U};

    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_init(NULL, &cfg));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_init(&rx, NULL));
    cfg.max_size = 0U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_canboot_rx_init(&rx, &cfg));
    cfg.max_size = SATLINK_CANBOOT_MAX_IMAGE + 1U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_canboot_rx_init(&rx, &cfg));
    cfg.max_size = 100U;
    cfg.write = NULL;
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_init(&rx, &cfg));
}

static void test_ping_reports_version_state_and_capacity(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t ping[1] = {0x01U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 0x1C000U, 0U);
    rsp = send(&rx, 1U, ping);

    TEST_ASSERT_EQUAL_HEX32(0x7E1U, rsp.id);
    TEST_ASSERT_EQUAL_HEX8(0x42U, rsp.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, rsp.data[1]); /* idle */
    TEST_ASSERT_EQUAL_HEX8(0x01U, rsp.data[2]); /* protocol version 1 */
    TEST_ASSERT_EQUAL_HEX8(0x00U, rsp.data[4]);
    TEST_ASSERT_EQUAL_HEX8(0xC0U, rsp.data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, rsp.data[6]); /* 0x0001C000 little endian */
}

static void test_begin_is_checked_for_size_and_length(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t zero[7] = {0x02U, 0U, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t big[7] = {0x02U, 0x01U, 0x04U, 0U, 0U, 0U, 0U}; /* 1025 bytes */
    static const uint8_t short_frame[3] = {0x02U, 0x10U, 0U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    rsp = send(&rx, 7U, zero);
    expect_nak(&rsp, 0x02U, SATLINK_CANBOOT_ERR_SIZE, 0U);
    rsp = send(&rx, 7U, big);
    expect_nak(&rsp, 0x02U, SATLINK_CANBOOT_ERR_SIZE, 0U);
    rsp = send(&rx, 3U, short_frame);
    expect_nak(&rsp, 0x02U, SATLINK_CANBOOT_ERR_LENGTH, 0U);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_IDLE, satlink_canboot_rx_state(&rx));
}

static void test_data_before_begin_is_refused(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t data[4] = {0x03U, 0x00U, 0x00U, 0xAAU};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    rsp = send(&rx, 4U, data);
    expect_nak(&rsp, 0x03U, SATLINK_CANBOOT_ERR_STATE, 0U);
    TEST_ASSERT_EQUAL_UINT(0U, sink.writes);
}

static void test_out_of_order_and_odd_sized_data_is_refused_without_storing_it(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t seq1[8] = {0x03U, 0x01U, 0x00U, 1U, 2U, 3U, 4U, 5U};
    static const uint8_t short_mid[7] = {0x03U, 0x00U, 0x00U, 1U, 2U, 3U, 4U}; /* 4 of 5 bytes */
    static const uint8_t no_seq[2] = {0x03U, 0x00U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    begin(&rx, 20U, 0x1234U);

    rsp = send(&rx, 8U, seq1);
    expect_nak(&rsp, 0x03U, SATLINK_CANBOOT_ERR_SEQ, 0U); /* expects 0 */
    rsp = send(&rx, 7U, short_mid);
    expect_nak(&rsp, 0x03U, SATLINK_CANBOOT_ERR_LENGTH, 0U);
    rsp = send(&rx, 2U, no_seq);
    expect_nak(&rsp, 0x03U, SATLINK_CANBOOT_ERR_LENGTH, 0U);
    TEST_ASSERT_EQUAL_UINT(0U, sink.writes);
}

static void test_end_and_boot_too_early_are_refused(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t end[1] = {0x04U};
    static const uint8_t boot[1] = {0x05U};
    static const uint8_t first[8] = {0x03U, 0x00U, 0x00U, 1U, 2U, 3U, 4U, 5U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    rsp = send(&rx, 1U, end);
    expect_nak(&rsp, 0x04U, SATLINK_CANBOOT_ERR_STATE, 0U); /* idle */

    begin(&rx, 20U, 0x1234U);
    rsp = send(&rx, 8U, first);
    TEST_ASSERT_EQUAL_HEX8(0U, rsp.data[0]); /* no acknowledgement yet: window not full */
    rsp = send(&rx, 1U, end);
    expect_nak(&rsp, 0x04U, SATLINK_CANBOOT_ERR_INCOMPLETE, 1U);
    rsp = send(&rx, 1U, boot);
    expect_nak(&rsp, 0x05U, SATLINK_CANBOOT_ERR_STATE, 1U);
}

static void test_a_failing_store_aborts_the_transfer(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t first[8] = {0x03U, 0x00U, 0x00U, 1U, 2U, 3U, 4U, 5U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    sink.fail_at_offset = 0U;
    begin(&rx, 20U, 0x1234U);

    rsp = send(&rx, 8U, first);
    expect_nak(&rsp, 0x03U, SATLINK_CANBOOT_ERR_WRITE, 0U);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_ERROR, satlink_canboot_rx_state(&rx));
}

static void test_unknown_opcodes_and_foreign_frames(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t junk[2] = {0x7FU, 0x00U};
    satlink_can_frame_t rsp;
    satlink_can_frame_t other = {.id = 0x123U, .dlc = 1U};
    bool have = true;

    make_rx(&rx, &sink, 1024U, 0U);
    rsp = send(&rx, 2U, junk);
    expect_nak(&rsp, 0x7FU, SATLINK_CANBOOT_ERR_OPCODE, 0U);

    other.data[0] = 0x01U; /* a PING, but on someone else's identifier */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(&rx, &other, &rsp, &have));
    TEST_ASSERT_FALSE(have);

    other.id = SATLINK_CANBOOT_ID_CMD;
    other.extended = true;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(&rx, &other, &rsp, &have));
    TEST_ASSERT_FALSE(have);

    other.extended = false;
    other.rtr = true;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(&rx, &other, &rsp, &have));
    TEST_ASSERT_FALSE(have);

    other.rtr = false;
    other.dlc = 0U;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(&rx, &other, &rsp, &have));
    TEST_ASSERT_FALSE(have);
}

static void test_repeats_and_restarts_are_handled(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    static const uint8_t data0[8] = {0x03U, 0x00U, 0x00U, 1U, 2U, 3U, 4U, 5U};
    static const uint8_t abort_cmd[1] = {0x06U};
    satlink_can_frame_t rsp;

    make_rx(&rx, &sink, 1024U, 0U);
    begin(&rx, 5U, satlink_crc16_ccitt(data0 + 3, 5U));

    rsp = send(&rx, 8U, data0); /* the last (only) chunk: acknowledged at once */
    expect_ack(&rsp, 0x03U, 1U);
    rsp = send(&rx, 8U, data0); /* the same frame again */
    expect_ack(&rsp, 0x03U, 1U);
    TEST_ASSERT_EQUAL_UINT(1U, sink.writes); /* stored only once */

    {
        const uint8_t end[1] = {0x04U};
        rsp = send(&rx, 1U, end);
        expect_ack(&rsp, 0x04U, 1U);
        TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_VERIFIED, satlink_canboot_rx_state(&rx));
        rsp = send(&rx, 1U, end); /* END again: the acknowledgement got lost */
        expect_ack(&rsp, 0x04U, 1U);
        TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_VERIFIED, satlink_canboot_rx_state(&rx));
    }

    begin(&rx, 20U, 0U); /* a new BEGIN starts over, even after a verified image */
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_RECEIVING, satlink_canboot_rx_state(&rx));
    TEST_ASSERT_FALSE(satlink_canboot_rx_boot_requested(&rx));

    rsp = send(&rx, 1U, abort_cmd);
    expect_ack(&rsp, 0x06U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_IDLE, satlink_canboot_rx_state(&rx));
}

static void test_enter_is_built_for_applications_and_acknowledged_by_a_bootloader(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_can_frame_t enter;
    satlink_can_frame_t rsp;
    bool have = false;

    satlink_canboot_make_enter(&enter);
    TEST_ASSERT_EQUAL_HEX32(0x7E0U, enter.id);
    TEST_ASSERT_EQUAL_UINT8(1U, enter.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x07U, enter.data[0]);
    TEST_ASSERT_FALSE(enter.extended);
    satlink_canboot_make_enter(NULL); /* must not crash */

    make_rx(&rx, &sink, 1024U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_canboot_rx_handle(&rx, &enter, &rsp, &have));
    TEST_ASSERT_TRUE(have);
    expect_ack(&rsp, 0x07U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_IDLE, satlink_canboot_rx_state(&rx)); /* changes nothing */
}

static void test_null_arguments(void)
{
    sink_t sink;
    satlink_canboot_rx_t rx;
    satlink_can_frame_t f = {.id = 0x7E0U, .dlc = 1U};
    satlink_can_frame_t rsp;
    bool have = false;

    make_rx(&rx, &sink, 1024U, 0U);
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_handle(NULL, &f, &rsp, &have));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_handle(&rx, NULL, &rsp, &have));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_handle(&rx, &f, NULL, &have));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_rx_handle(&rx, &f, &rsp, NULL));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_canboot_tx_on_response(NULL, &f));
    TEST_ASSERT_FALSE(satlink_canboot_tx_next(NULL, &f));
    TEST_ASSERT_EQUAL(SATLINK_CANBOOT_RX_IDLE, satlink_canboot_rx_state(NULL));
    TEST_ASSERT_FALSE(satlink_canboot_rx_boot_requested(NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, satlink_canboot_rx_image_size(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, satlink_canboot_tx_progress(NULL));
    satlink_canboot_tx_on_timeout(NULL); /* must not crash */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_small_image_goes_over_in_the_expected_frames);
    RUN_TEST(test_frame_layouts_on_the_wire);
    RUN_TEST(test_images_of_every_awkward_size_and_window);
    RUN_TEST(test_a_lossy_bus_still_delivers_the_image);
    RUN_TEST(test_a_very_lossy_bus_with_a_window_of_one);
    RUN_TEST(test_duplicated_frames_are_harmless);
    RUN_TEST(test_a_corrupted_payload_is_caught_by_the_crc_and_boot_is_refused);
    RUN_TEST(test_the_sender_gives_up_after_its_retries);
    RUN_TEST(test_a_receiver_that_is_ahead_of_a_rewound_sender_is_followed);
    RUN_TEST(test_stale_and_impossible_acknowledgements);
    RUN_TEST(test_the_sender_ignores_frames_that_are_not_responses);
    RUN_TEST(test_sender_init_rejects_bad_arguments);
    RUN_TEST(test_receiver_init_rejects_bad_configuration);
    RUN_TEST(test_ping_reports_version_state_and_capacity);
    RUN_TEST(test_begin_is_checked_for_size_and_length);
    RUN_TEST(test_data_before_begin_is_refused);
    RUN_TEST(test_out_of_order_and_odd_sized_data_is_refused_without_storing_it);
    RUN_TEST(test_end_and_boot_too_early_are_refused);
    RUN_TEST(test_a_failing_store_aborts_the_transfer);
    RUN_TEST(test_unknown_opcodes_and_foreign_frames);
    RUN_TEST(test_repeats_and_restarts_are_handled);
    RUN_TEST(test_enter_is_built_for_applications_and_acknowledged_by_a_bootloader);
    RUN_TEST(test_null_arguments);
    return UNITY_END();
}
