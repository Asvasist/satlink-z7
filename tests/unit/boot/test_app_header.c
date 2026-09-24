/**
 * @file test_app_header.c
 * @brief Validation of a housekeeping application image already in memory.
 *
 * @verifies SRS-HKC-003
 */
#include <stddef.h>
#include <stdint.h>

#include "satlink/boot/app_header.h"

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

/* Produced by tools/hkc/mkapp.py from 16 zero bytes + "SatLink hkc app"; the same bytes are
 * checked in tools/hkc/tests/test_mkapp.py, so the tool and this validator cannot drift apart. */
static const uint8_t k_golden[31] = {0x53U, 0x4CU, 0x41U, 0x50U, 0x01U, 0x00U, 0x10U, 0x00U,
                                     0x1FU, 0x00U, 0x00U, 0x00U, 0x4CU, 0x6CU, 0x00U, 0x00U,
                                     0x53U, 0x61U, 0x74U, 0x4CU, 0x69U, 0x6EU, 0x6BU, 0x20U,
                                     0x68U, 0x6BU, 0x63U, 0x20U, 0x61U, 0x70U, 0x70U};

static void test_the_image_from_the_tool_validates(void)
{
    size_t entry = 0U;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_app_validate(k_golden, sizeof(k_golden), &entry));
    TEST_ASSERT_EQUAL_UINT(16U, entry);
}

static void test_extra_bytes_after_the_image_are_fine(void)
{
    uint8_t memory[64];
    size_t entry = 0U;

    fill_bytes(memory, 0xEE, sizeof(memory)); /* stale memory after the image */
    copy_bytes(memory, k_golden, sizeof(k_golden));

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_app_validate(memory, sizeof(memory), &entry));
}

static void test_header_fields_are_decoded(void)
{
    satlink_app_header_t header;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_app_header_parse(k_golden, &header));
    TEST_ASSERT_EQUAL_HEX32(SATLINK_APP_MAGIC, header.magic);
    TEST_ASSERT_EQUAL_UINT16(1U, header.header_version);
    TEST_ASSERT_EQUAL_UINT16(16U, header.header_len);
    TEST_ASSERT_EQUAL_UINT32(31U, header.image_size);
    TEST_ASSERT_EQUAL_HEX16(0x6C4CU, header.crc16);
    TEST_ASSERT_EQUAL_UINT16(0U, header.reserved);
}

static void test_a_flipped_bit_in_the_code_is_a_crc_error(void)
{
    uint8_t memory[31];
    size_t entry = 0U;

    copy_bytes(memory, k_golden, sizeof(memory));
    memory[20] = (uint8_t)(memory[20] ^ 0x08U);

    TEST_ASSERT_EQUAL(SATLINK_ERR_CRC, satlink_app_validate(memory, sizeof(memory), &entry));
}

static void test_blank_or_foreign_memory_is_not_an_application(void)
{
    uint8_t memory[64];
    size_t entry = 0U;

    fill_bytes(memory, 0x00, sizeof(memory));
    TEST_ASSERT_EQUAL(SATLINK_ERR_STATE, satlink_app_validate(memory, sizeof(memory), &entry));
    fill_bytes(memory, 0xFF, sizeof(memory));
    TEST_ASSERT_EQUAL(SATLINK_ERR_STATE, satlink_app_validate(memory, sizeof(memory), &entry));
}

static void test_inconsistent_headers_are_range_errors(void)
{
    uint8_t memory[31];
    size_t entry = 0U;

    copy_bytes(memory, k_golden, sizeof(memory));
    memory[8] = 0xFFU; /* size 255 does not fit the 31 bytes available */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_app_validate(memory, sizeof(memory), &entry));

    copy_bytes(memory, k_golden, sizeof(memory));
    memory[8] = 0x10U; /* size equal to the header: no code at all */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_app_validate(memory, sizeof(memory), &entry));

    copy_bytes(memory, k_golden, sizeof(memory));
    memory[4] = 0x02U; /* an unknown header version */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_app_validate(memory, sizeof(memory), &entry));

    copy_bytes(memory, k_golden, sizeof(memory));
    memory[6] = 0x20U; /* a different header length than this bootloader understands */
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_app_validate(memory, sizeof(memory), &entry));
}

static void test_less_memory_than_a_header_and_null_arguments(void)
{
    size_t entry = 0U;

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_app_validate(k_golden, 15U, &entry));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_app_validate(NULL, 31U, &entry));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_app_validate(k_golden, 31U, NULL));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_app_header_parse(NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_image_from_the_tool_validates);
    RUN_TEST(test_extra_bytes_after_the_image_are_fine);
    RUN_TEST(test_header_fields_are_decoded);
    RUN_TEST(test_a_flipped_bit_in_the_code_is_a_crc_error);
    RUN_TEST(test_blank_or_foreign_memory_is_not_an_application);
    RUN_TEST(test_inconsistent_headers_are_range_errors);
    RUN_TEST(test_less_memory_than_a_header_and_null_arguments);
    return UNITY_END();
}
