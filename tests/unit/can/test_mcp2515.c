/**
 * @file test_mcp2515.c
 * @brief MCP2515 driver against a small simulator of the chip's SPI instruction set.
 *
 * @verifies SRS-HKC-002
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "satlink/can/mcp2515.h"

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

/* Just enough of the MCP2515 to test the driver: registers, mode changes that take a while,
 * two receive buffers, one transmit buffer. */
typedef struct
{
    uint8_t reg[0x80];
    uint8_t mode_now; /* value CANSTAT reports, OPMOD in bits 7..5 */
    uint8_t mode_requested;
    uint32_t mode_delay; /* CANSTAT reads before a requested mode becomes visible */
    uint32_t mode_reads;
    bool dead;      /* no chip: reads return 0x00 and reset does nothing */
    bool fail_next; /* make the next transfer fail with SATLINK_ERR_IO */
    bool has_rx[2];
    uint8_t rx[2][13];
    uint8_t last_read_rx_instr;
    uint8_t tx_buf[13];
    size_t tx_len;
    unsigned rts_count;
    unsigned transfers;
} fake_mcp_t;

static void fake_reset(fake_mcp_t *chip)
{
    fill_bytes(chip->reg, 0, sizeof(chip->reg));
    chip->mode_now = 0x80U; /* configuration mode after reset */
    chip->mode_requested = 0x80U;
    chip->mode_reads = 0U;
}

static void fake_init(fake_mcp_t *chip)
{
    fill_bytes(chip, 0, sizeof(*chip));
    fake_reset(chip);
}

static satlink_status_t fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    fake_mcp_t *chip = (fake_mcp_t *)ctx;
    satlink_status_t status = SATLINK_OK;

    chip->transfers++;
    fill_bytes(rx, 0, len);

    if (chip->fail_next)
    {
        chip->fail_next = false;
        status = SATLINK_ERR_IO;
    }
    else if (tx[0] == 0xC0U)
    {
        if (!chip->dead)
        {
            fake_reset(chip);
        }
    }
    else if (tx[0] == 0x03U)
    {
        if ((tx[1] == 0x0EU) && !chip->dead)
        {
            if ((chip->mode_now != chip->mode_requested) && (++chip->mode_reads > chip->mode_delay))
            {
                chip->mode_now = chip->mode_requested;
            }
            rx[2] = chip->mode_now;
        }
        else if (!chip->dead)
        {
            rx[2] = chip->reg[tx[1]];
        }
    }
    else if (tx[0] == 0x02U)
    {
        for (size_t i = 2U; i < len; ++i)
        {
            chip->reg[tx[1] + (i - 2U)] = tx[i];
        }
    }
    else if (tx[0] == 0x05U)
    {
        chip->reg[tx[1]] = (uint8_t)((chip->reg[tx[1]] & (uint8_t)~tx[2]) | (tx[3] & tx[2]));
        if (tx[1] == 0x0FU)
        {
            chip->mode_requested = (uint8_t)(tx[3] & tx[2] & 0xE0U);
            chip->mode_reads = 0U;
        }
    }
    else if (tx[0] == 0xA0U)
    {
        rx[1] = (uint8_t)((chip->has_rx[0] ? 0x01U : 0U) | (chip->has_rx[1] ? 0x02U : 0U));
    }
    else if ((tx[0] == 0x90U) || (tx[0] == 0x94U))
    {
        const size_t n = (tx[0] == 0x90U) ? 0U : 1U;

        chip->last_read_rx_instr = tx[0];
        copy_bytes(&rx[1], chip->rx[n], 13U);
        chip->has_rx[n] = false;
    }
    else if (tx[0] == 0x40U)
    {
        chip->tx_len = len - 1U;
        copy_bytes(chip->tx_buf, &tx[1], chip->tx_len);
    }
    else if (tx[0] == 0x81U)
    {
        chip->reg[0x30] = (uint8_t)(chip->reg[0x30] | 0x08U); /* TXREQ */
        chip->rts_count++;
    }
    else
    {
        status = SATLINK_ERR_IO;
    }

    return status;
}

static satlink_mcp2515_t make_dev(fake_mcp_t *chip)
{
    satlink_mcp2515_t dev;

    fake_init(chip);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_init(&dev, fake_transfer, NULL, chip));
    return dev;
}

static void test_timing_16mhz_500k_uses_16_quanta_and_87_5_percent_sample_point(void)
{
    satlink_mcp2515_timing_t timing;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_calc_timing(16000000U, 500000U, &timing));
    /* BRP = 0, PROP = 6, PS1 = 7, PS2 = 2: 1 + 6 + 7 + 2 = 16 quanta, sample after 14 of 16. */
    TEST_ASSERT_EQUAL_HEX8(0x00U, timing.cnf1);
    TEST_ASSERT_EQUAL_HEX8(0xB5U, timing.cnf2);
    TEST_ASSERT_EQUAL_HEX8(0x01U, timing.cnf3);
}

static void test_timing_8mhz_125k_needs_a_prescaler(void)
{
    satlink_mcp2515_timing_t timing;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_calc_timing(8000000U, 125000U, &timing));
    TEST_ASSERT_EQUAL_HEX8(0x01U, timing.cnf1); /* BRP = 1 */
    TEST_ASSERT_EQUAL_HEX8(0xB5U, timing.cnf2);
    TEST_ASSERT_EQUAL_HEX8(0x01U, timing.cnf3);
}

static void test_timing_falls_back_to_fewer_quanta_when_16_does_not_divide(void)
{
    satlink_mcp2515_timing_t timing;

    /* 20 MHz at 1 Mbit/s: only 10 quanta per bit fit (BRP = 0). */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_calc_timing(20000000U, 1000000U, &timing));
    TEST_ASSERT_EQUAL_HEX8(0x00U, timing.cnf1);
    TEST_ASSERT_EQUAL_HEX8(0x9AU, timing.cnf2); /* PS1 = 4, PROP = 3 */
    TEST_ASSERT_EQUAL_HEX8(0x01U, timing.cnf3); /* PS2 = 2 */
}

static void test_timing_rejects_impossible_requests(void)
{
    satlink_mcp2515_timing_t timing;

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_calc_timing(16000000U, 999999U, &timing));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_calc_timing(16000000U, 2000000U, &timing));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_calc_timing(0U, 500000U, &timing));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_calc_timing(16000000U, 0U, &timing));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_calc_timing(16000000U, 500000U, NULL));
}

static void test_configure_programs_timing_filters_and_mode(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_configure(&dev, 16000000U, 500000U,
                                                            SATLINK_MCP2515_MODE_NORMAL));

    TEST_ASSERT_EQUAL_HEX8(0x01U, chip.reg[0x28]); /* CNF3 */
    TEST_ASSERT_EQUAL_HEX8(0xB5U, chip.reg[0x29]); /* CNF2 */
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip.reg[0x2A]); /* CNF1 */
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip.reg[0x2B]); /* CANINTE: polling, no interrupts */
    TEST_ASSERT_EQUAL_HEX8(0x64U, chip.reg[0x60]); /* RXB0CTRL: any frame, roll over */
    TEST_ASSERT_EQUAL_HEX8(0x60U, chip.reg[0x70]); /* RXB1CTRL: any frame */
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip.mode_now);  /* normal mode */
}

static void test_configure_can_select_loopback(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_configure(&dev, 16000000U, 500000U,
                                                            SATLINK_MCP2515_MODE_LOOPBACK));
    TEST_ASSERT_EQUAL_HEX8(0x40U, chip.mode_now);
}

static void test_configure_reports_a_missing_controller(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);

    chip.dead = true;
    TEST_ASSERT_EQUAL(SATLINK_ERR_IO, satlink_mcp2515_configure(&dev, 16000000U, 500000U,
                                                                SATLINK_MCP2515_MODE_NORMAL));
}

static void test_mode_change_polls_until_the_controller_follows(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);

    chip.mode_delay = 5U;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_set_mode(&dev, SATLINK_MCP2515_MODE_NORMAL));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip.mode_now);
}

static void test_mode_change_times_out_when_the_controller_never_follows(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);

    chip.mode_delay = 1000U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_TIMEOUT,
                      satlink_mcp2515_set_mode(&dev, SATLINK_MCP2515_MODE_NORMAL));
}

static void test_send_standard_frame_loads_the_buffer_and_requests_transmission(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    const satlink_can_frame_t frame = {
        .id = 0x123U, .dlc = 3U, .extended = false, .rtr = false, .data = {1U, 2U, 3U}};
    const uint8_t expected[] = {0x24U, 0x60U, 0x00U, 0x00U, 0x03U, 1U, 2U, 3U};

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_send(&dev, &frame));

    TEST_ASSERT_EQUAL_UINT(sizeof(expected), chip.tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, chip.tx_buf, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT(1U, chip.rts_count);
}

static void test_send_extended_frame_splits_the_identifier(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    const satlink_can_frame_t frame = {
        .id = 0x18DAF110U, .dlc = 2U, .extended = true, .rtr = false, .data = {0xAAU, 0x55U}};
    const uint8_t expected[] = {0xC6U, 0xCAU, 0xF1U, 0x10U, 0x02U, 0xAAU, 0x55U};

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_send(&dev, &frame));

    TEST_ASSERT_EQUAL_UINT(sizeof(expected), chip.tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, chip.tx_buf, sizeof(expected));
}

static void test_send_marks_remote_frames_in_the_length_byte(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    const satlink_can_frame_t frame = {.id = 0x7FFU, .dlc = 0U, .extended = false, .rtr = true};

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_send(&dev, &frame));
    TEST_ASSERT_EQUAL_HEX8(0x40U, chip.tx_buf[4]);
}

static void test_send_is_busy_until_the_previous_frame_left(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    const satlink_can_frame_t frame = {.id = 0x100U, .dlc = 1U, .data = {0x42U}};

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_send(&dev, &frame));
    TEST_ASSERT_EQUAL(SATLINK_ERR_BUSY, satlink_mcp2515_send(&dev, &frame));
    TEST_ASSERT_EQUAL_UINT(1U, chip.rts_count);

    chip.reg[0x30] = 0U; /* the controller finished transmitting */
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_send(&dev, &frame));
}

static void test_send_rejects_bad_frames_before_touching_the_bus(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame = {.id = 0x100U, .dlc = 9U};

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_send(&dev, &frame));
    frame.dlc = 1U;
    frame.id = 0x800U; /* too big for a standard identifier */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_send(&dev, &frame));
    frame.extended = true;
    frame.id = 0x20000000U; /* too big for 29 bits */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_mcp2515_send(&dev, &frame));
    TEST_ASSERT_EQUAL_UINT(0U, chip.transfers);
}

static void test_receive_decodes_a_standard_frame(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame;
    const uint8_t raw[13] = {0x24U, 0x60U, 0x00U, 0x00U, 0x03U, 1U,   2U,
                             3U,    0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU};

    copy_bytes(chip.rx[0], raw, sizeof(raw));
    chip.has_rx[0] = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_receive(&dev, &frame));
    TEST_ASSERT_EQUAL_HEX32(0x123U, frame.id);
    TEST_ASSERT_EQUAL_UINT8(3U, frame.dlc);
    TEST_ASSERT_FALSE(frame.extended);
    TEST_ASSERT_FALSE(frame.rtr);
    TEST_ASSERT_EQUAL_HEX8(1U, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(3U, frame.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0U, frame.data[3]); /* bytes past the length are not passed on */

    TEST_ASSERT_EQUAL(SATLINK_ERR_EMPTY, satlink_mcp2515_receive(&dev, &frame));
}

static void test_receive_decodes_an_extended_remote_frame(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame;
    const uint8_t raw[13] = {0xC6U, 0xCAU, 0xF1U, 0x10U, 0x42U};

    copy_bytes(chip.rx[0], raw, sizeof(raw));
    chip.has_rx[0] = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_receive(&dev, &frame));
    TEST_ASSERT_EQUAL_HEX32(0x18DAF110U, frame.id);
    TEST_ASSERT_TRUE(frame.extended);
    TEST_ASSERT_TRUE(frame.rtr);
    TEST_ASSERT_EQUAL_UINT8(2U, frame.dlc);
}

static void test_receive_decodes_a_standard_remote_frame(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame;
    const uint8_t raw[13] = {0x24U, 0x70U, 0x00U, 0x00U, 0x00U};

    copy_bytes(chip.rx[0], raw, sizeof(raw));
    chip.has_rx[0] = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_receive(&dev, &frame));
    TEST_ASSERT_EQUAL_HEX32(0x123U, frame.id);
    TEST_ASSERT_FALSE(frame.extended);
    TEST_ASSERT_TRUE(frame.rtr);
}

static void test_receive_reads_buffer_1_when_buffer_0_is_empty(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame;
    const uint8_t raw[13] = {0x20U, 0x00U, 0x00U, 0x00U, 0x01U, 0x99U};

    copy_bytes(chip.rx[1], raw, sizeof(raw));
    chip.has_rx[1] = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_receive(&dev, &frame));
    TEST_ASSERT_EQUAL_HEX8(0x94U, chip.last_read_rx_instr);
    TEST_ASSERT_EQUAL_HEX32(0x100U, frame.id);
    TEST_ASSERT_EQUAL_HEX8(0x99U, frame.data[0]);
}

static void test_receive_clamps_a_corrupt_length(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame;
    const uint8_t raw[13] = {0x20U, 0x00U, 0x00U, 0x00U, 0x0FU};

    copy_bytes(chip.rx[0], raw, sizeof(raw));
    chip.has_rx[0] = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_receive(&dev, &frame));
    TEST_ASSERT_EQUAL_UINT8(8U, frame.dlc);
}

static void test_error_flags_and_overflow_clear(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    uint8_t flags = 0U;

    chip.reg[0x2D] = 0xC5U;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_error_flags(&dev, &flags));
    TEST_ASSERT_EQUAL_HEX8(0xC5U, flags);

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_mcp2515_clear_overflow(&dev));
    TEST_ASSERT_EQUAL_HEX8(0x05U, chip.reg[0x2D]);
}

static void test_bus_failures_and_null_arguments_are_reported(void)
{
    fake_mcp_t chip;
    satlink_mcp2515_t dev = make_dev(&chip);
    satlink_can_frame_t frame = {.id = 1U, .dlc = 0U};

    chip.fail_next = true;
    TEST_ASSERT_EQUAL(SATLINK_ERR_IO, satlink_mcp2515_receive(&dev, &frame));
    chip.fail_next = true;
    TEST_ASSERT_EQUAL(SATLINK_ERR_IO, satlink_mcp2515_send(&dev, &frame));

    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_init(NULL, fake_transfer, NULL, &chip));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_init(&dev, NULL, NULL, &chip));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_send(NULL, &frame));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_send(&dev, NULL));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_receive(&dev, NULL));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_error_flags(&dev, NULL));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_mcp2515_configure(NULL, 16000000U, 500000U,
                                                                  SATLINK_MCP2515_MODE_NORMAL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timing_16mhz_500k_uses_16_quanta_and_87_5_percent_sample_point);
    RUN_TEST(test_timing_8mhz_125k_needs_a_prescaler);
    RUN_TEST(test_timing_falls_back_to_fewer_quanta_when_16_does_not_divide);
    RUN_TEST(test_timing_rejects_impossible_requests);
    RUN_TEST(test_configure_programs_timing_filters_and_mode);
    RUN_TEST(test_configure_can_select_loopback);
    RUN_TEST(test_configure_reports_a_missing_controller);
    RUN_TEST(test_mode_change_polls_until_the_controller_follows);
    RUN_TEST(test_mode_change_times_out_when_the_controller_never_follows);
    RUN_TEST(test_send_standard_frame_loads_the_buffer_and_requests_transmission);
    RUN_TEST(test_send_extended_frame_splits_the_identifier);
    RUN_TEST(test_send_marks_remote_frames_in_the_length_byte);
    RUN_TEST(test_send_is_busy_until_the_previous_frame_left);
    RUN_TEST(test_send_rejects_bad_frames_before_touching_the_bus);
    RUN_TEST(test_receive_decodes_a_standard_frame);
    RUN_TEST(test_receive_decodes_an_extended_remote_frame);
    RUN_TEST(test_receive_decodes_a_standard_remote_frame);
    RUN_TEST(test_receive_reads_buffer_1_when_buffer_0_is_empty);
    RUN_TEST(test_receive_clamps_a_corrupt_length);
    RUN_TEST(test_error_flags_and_overflow_clear);
    RUN_TEST(test_bus_failures_and_null_arguments_are_reported);
    return UNITY_END();
}
