/**
 * @file test_crc16_ccitt.c
 * @brief Unit tests for the CCSDS CRC-16-CCITT reference implementation.
 *
 * @verifies SRS-LIB-001
 */
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/crc16_ccitt.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t k_check_input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static void test_check_value_matches_crc16_ccitt_false(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1U, satlink_crc16_ccitt(k_check_input, sizeof(k_check_input)));
}

static void test_empty_buffer_returns_init_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(SATLINK_CRC16_CCITT_INIT, satlink_crc16_ccitt(k_check_input, 0U));
}

static void test_null_data_leaves_crc_unchanged(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x1234U, satlink_crc16_ccitt_update(0x1234U, NULL, 5U));
    TEST_ASSERT_EQUAL_HEX16(SATLINK_CRC16_CCITT_INIT, satlink_crc16_ccitt(NULL, 0U));
}

static void test_incremental_update_equals_single_pass(void)
{
    uint16_t crc = (uint16_t)SATLINK_CRC16_CCITT_INIT;
    crc = satlink_crc16_ccitt_update(crc, &k_check_input[0], 4U);
    crc = satlink_crc16_ccitt_update(crc, &k_check_input[4], 5U);
    TEST_ASSERT_EQUAL_HEX16(satlink_crc16_ccitt(k_check_input, sizeof(k_check_input)), crc);
}

static void test_appended_fecf_gives_zero_residue(void)
{
    uint8_t frame[sizeof(k_check_input) + 2U];
    for (size_t i = 0U; i < sizeof(k_check_input); ++i)
    {
        frame[i] = k_check_input[i];
    }
    const uint16_t fecf = satlink_crc16_ccitt(k_check_input, sizeof(k_check_input));
    frame[sizeof(k_check_input)] = (uint8_t)(fecf >> 8U);
    frame[sizeof(k_check_input) + 1U] = (uint8_t)(fecf & 0xFFU);

    TEST_ASSERT_EQUAL_HEX16(0x0000U, satlink_crc16_ccitt(frame, sizeof(frame)));
}

static void test_single_bit_error_is_detected(void)
{
    uint8_t corrupted[sizeof(k_check_input)];
    for (size_t i = 0U; i < sizeof(k_check_input); ++i)
    {
        corrupted[i] = k_check_input[i];
    }
    corrupted[3] = (uint8_t)(corrupted[3] ^ 0x10U);

    TEST_ASSERT_NOT_EQUAL(satlink_crc16_ccitt(k_check_input, sizeof(k_check_input)),
                          satlink_crc16_ccitt(corrupted, sizeof(corrupted)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value_matches_crc16_ccitt_false);
    RUN_TEST(test_empty_buffer_returns_init_value);
    RUN_TEST(test_null_data_leaves_crc_unchanged);
    RUN_TEST(test_incremental_update_equals_single_pass);
    RUN_TEST(test_appended_fecf_gives_zero_residue);
    RUN_TEST(test_single_bit_error_is_detected);
    return UNITY_END();
}
