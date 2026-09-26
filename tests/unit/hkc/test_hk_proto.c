/**
 * @file test_hk_proto.c
 * @brief Housekeeping CAN frames: encode/decode round trips, exact wire bytes, range checks,
 *        XADC conversions.
 *
 * @verifies SRS-HKC-003
 */
#include <stddef.h>
#include <stdint.h>

#include "satlink/hkc/hk_proto.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_env_round_trip_and_wire_format(void)
{
    const satlink_hk_env_t env = {-1234, 1000, 1800, 1001};
    satlink_can_frame_t frame;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_encode_env(&env, &frame));
    TEST_ASSERT_EQUAL_HEX16(0x100U, frame.id);
    TEST_ASSERT_EQUAL_UINT8(8U, frame.dlc);
    const uint8_t expected[8] = {0x2E, 0xFB, 0xE8, 0x03, 0x08, 0x07, 0xE9, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame.data, 8U);

    satlink_hk_env_t out;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_env(&frame, &out));
    TEST_ASSERT_EQUAL_INT16(-1234, out.die_temp_centi_c);
    TEST_ASSERT_EQUAL_UINT16(1000U, out.vccint_mv);
    TEST_ASSERT_EQUAL_UINT16(1800U, out.vccaux_mv);
    TEST_ASSERT_EQUAL_UINT16(1001U, out.vbram_mv);
}

static void test_decode_rejects_wrong_id_or_short_frame(void)
{
    const satlink_hk_env_t env = {0, 1, 2, 3};
    satlink_can_frame_t frame;
    satlink_hk_env_t out;
    (void)satlink_hk_encode_env(&env, &frame);
    frame.dlc = 7U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode_env(&frame, &out));
    frame.dlc = 8U;
    frame.id = 0x101U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode_env(&frame, &out));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_hk_decode_env(NULL, &out));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_hk_encode_env(&env, NULL));
}

static void test_supply_and_status_round_trip(void)
{
    const satlink_hk_supply_t supply = {1000, 1800, 1350};
    satlink_can_frame_t frame;
    satlink_hk_supply_t supply_out;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_encode_supply(&supply, &frame));
    TEST_ASSERT_EQUAL_UINT8(6U, frame.dlc);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_supply(&frame, &supply_out));
    TEST_ASSERT_EQUAL_UINT16(1350U, supply_out.vcco_ddr_mv);

    const satlink_hk_status_t status = {0x01020304UL, SATLINK_HK_RESET_WATCHDOG, 0x0AU, 17U,
                                        SATLINK_HK_ERR_CAN_TX};
    satlink_hk_status_t status_out;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_encode_status(&status, &frame));
    TEST_ASSERT_EQUAL_HEX8(0x04U, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01U, frame.data[3]);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_status(&frame, &status_out));
    TEST_ASSERT_EQUAL_HEX32(0x01020304UL, status_out.uptime_s);
    TEST_ASSERT_EQUAL_UINT8(SATLINK_HK_RESET_WATCHDOG, status_out.reset_cause);
    TEST_ASSERT_EQUAL_UINT8(0x0AU, status_out.switches);
    TEST_ASSERT_EQUAL_UINT8(17U, status_out.cmd_count);
    TEST_ASSERT_EQUAL_UINT8(SATLINK_HK_ERR_CAN_TX, status_out.error_flags);
}

static void test_time_sync_validates_milliseconds(void)
{
    satlink_hk_time_t time = {1760000000UL, 999U};
    satlink_can_frame_t frame;
    satlink_hk_time_t out;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_encode_time(&time, &frame));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_time(&frame, &out));
    TEST_ASSERT_EQUAL_UINT32(1760000000UL, out.unix_s);
    TEST_ASSERT_EQUAL_UINT16(999U, out.millis);

    time.millis = 1000U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_encode_time(&time, &frame));
    frame.data[4] = 0xE8; /* 1000 */
    frame.data[5] = 0x03;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode_time(&frame, &out));
}

static void test_command_and_ack(void)
{
    const uint8_t args[2] = {0xB0, 0x07};
    satlink_can_frame_t frame;
    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_hk_encode_command(SATLINK_HK_CMD_ENTER_BOOT, args, 2U, &frame));
    TEST_ASSERT_EQUAL_HEX16(0x180U, frame.id);
    TEST_ASSERT_EQUAL_UINT8(3U, frame.dlc);

    satlink_hk_command_t cmd;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_command(&frame, &cmd));
    TEST_ASSERT_EQUAL_UINT8(SATLINK_HK_CMD_ENTER_BOOT, cmd.opcode);
    TEST_ASSERT_EQUAL_UINT8(2U, cmd.argc);
    TEST_ASSERT_EQUAL_HEX8(0x07U, cmd.argv[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, cmd.argv[2]);

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_encode_command(1U, args, 8U, &frame));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_hk_encode_command(1U, NULL, 1U, &frame));
    frame.dlc = 0U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode_command(&frame, &cmd));

    const satlink_hk_ack_t ack = {SATLINK_HK_CMD_GET_VERSION, SATLINK_HK_ACK_OK, 3U, {1, 2, 3}};
    satlink_hk_ack_t ack_out;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_encode_ack(&ack, &frame));
    TEST_ASSERT_EQUAL_UINT8(5U, frame.dlc);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode_ack(&frame, &ack_out));
    TEST_ASSERT_EQUAL_UINT8(3U, ack_out.datac);
    TEST_ASSERT_EQUAL_UINT8(3U, ack_out.datav[2]);
}

static void test_xadc_conversions(void)
{
    /* UG480: code 0x9A0 (2464) -> 2464 * 503.975 / 4096 - 273.15 = 30.02 degC */
    TEST_ASSERT_INT16_WITHIN(1, 3002, satlink_hk_xadc_temp_centi_c(2464U));
    TEST_ASSERT_EQUAL_INT16(-27315, satlink_hk_xadc_temp_centi_c(0U));
    /* 1.0 V = 1365.33 codes */
    TEST_ASSERT_UINT16_WITHIN(1U, 1000U, satlink_hk_xadc_supply_mv(1366U));
    TEST_ASSERT_EQUAL_UINT16(2999U, satlink_hk_xadc_supply_mv(0xFFFFU)); /* masked to 12 bits */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_env_round_trip_and_wire_format);
    RUN_TEST(test_decode_rejects_wrong_id_or_short_frame);
    RUN_TEST(test_supply_and_status_round_trip);
    RUN_TEST(test_time_sync_validates_milliseconds);
    RUN_TEST(test_command_and_ack);
    RUN_TEST(test_xadc_conversions);
    return UNITY_END();
}
