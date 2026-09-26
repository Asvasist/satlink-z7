/**
 * @file test_crc32.c
 * @brief CRC-32 reference values (same parameters as zlib).
 *
 * @verifies SRS-LIB-004
 */
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/crc32.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t k_check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static void test_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, satlink_crc32(k_check, sizeof(k_check)));
}

static void test_empty_and_null(void)
{
    TEST_ASSERT_EQUAL_HEX32(0U, satlink_crc32(k_check, 0U));
    TEST_ASSERT_EQUAL_HEX32(0x12345678UL, satlink_crc32_update(0x12345678UL, NULL, 3U));
}

static void test_chained_equals_single_pass(void)
{
    uint32_t crc = SATLINK_CRC32_INIT;
    crc = satlink_crc32_update(crc, &k_check[0], 2U);
    crc = satlink_crc32_update(crc, &k_check[2], 7U);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, crc);
}

static void test_known_vector(void)
{
    /* zlib.crc32(b"The quick brown fox jumps over the lazy dog") */
    static const char k_fox[] = "The quick brown fox jumps over the lazy dog";
    TEST_ASSERT_EQUAL_HEX32(0x414FA339UL,
                            satlink_crc32((const uint8_t *)k_fox, sizeof(k_fox) - 1U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty_and_null);
    RUN_TEST(test_chained_equals_single_pass);
    RUN_TEST(test_known_vector);
    return UNITY_END();
}
