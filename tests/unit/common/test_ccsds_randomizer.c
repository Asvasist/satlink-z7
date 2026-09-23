/**
 * @file test_ccsds_randomizer.c
 * @brief Unit tests for the CCSDS pseudo-randomizer reference implementation.
 *
 * @verifies SRS-LIB-002
 */
#include <stddef.h>
#include <stdint.h>

#include "satlink/common/ccsds_randomizer.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* First bytes of the CCSDS 131.0-B pseudo-randomizer sequence. */
static const uint8_t k_expected_prefix[] = {0xFFU, 0x48U, 0x0EU, 0xC0U, 0x9AU, 0x0DU, 0x70U, 0xBCU};

static void test_sequence_starts_with_ccsds_reference_bytes(void)
{
    satlink_ccsds_randomizer_t ctx;
    satlink_ccsds_randomizer_reset(&ctx);

    for (size_t i = 0U; i < sizeof(k_expected_prefix); ++i)
    {
        TEST_ASSERT_EQUAL_HEX8(k_expected_prefix[i], satlink_ccsds_randomizer_next(&ctx));
    }
}

static void test_sequence_repeats_after_255_bytes(void)
{
    uint8_t seq[2U * SATLINK_CCSDS_RANDOMIZER_PERIOD_BYTES];
    satlink_ccsds_randomizer_t ctx;
    satlink_ccsds_randomizer_reset(&ctx);

    for (size_t i = 0U; i < sizeof(seq); ++i)
    {
        seq[i] = satlink_ccsds_randomizer_next(&ctx);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&seq[0], &seq[SATLINK_CCSDS_RANDOMIZER_PERIOD_BYTES],
                                 SATLINK_CCSDS_RANDOMIZER_PERIOD_BYTES);
}

static void test_randomizing_twice_restores_the_frame(void)
{
    uint8_t frame[64];
    uint8_t original[64];
    for (size_t i = 0U; i < sizeof(frame); ++i)
    {
        frame[i] = (uint8_t)(i * 7U);
        original[i] = frame[i];
    }

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(frame, sizeof(frame)));
    TEST_ASSERT_NOT_EQUAL(0, (frame[0] ^ original[0]));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original, frame, sizeof(frame));
}

static void test_every_frame_restarts_from_the_seed(void)
{
    uint8_t first[16] = {0U};
    uint8_t second[16] = {0U};

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(first, sizeof(first)));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(second, sizeof(second)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, second, sizeof(first));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k_expected_prefix, first, sizeof(k_expected_prefix));
}

static void test_split_apply_continues_the_sequence(void)
{
    uint8_t whole[20] = {0U};
    uint8_t split[20] = {0U};
    satlink_ccsds_randomizer_t ctx;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(whole, sizeof(whole)));

    satlink_ccsds_randomizer_reset(&ctx);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomizer_apply(&ctx, &split[0], 7U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomizer_apply(&ctx, &split[7], 13U));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(whole, split, sizeof(whole));
}

static void test_null_arguments_are_rejected(void)
{
    satlink_ccsds_randomizer_t ctx;
    uint8_t byte = 0U;
    satlink_ccsds_randomizer_reset(&ctx);

    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_ccsds_randomize_frame(NULL, 4U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ccsds_randomize_frame(NULL, 0U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_ccsds_randomizer_apply(NULL, &byte, 1U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, satlink_ccsds_randomizer_next(NULL));
    satlink_ccsds_randomizer_reset(NULL); /* must not crash */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sequence_starts_with_ccsds_reference_bytes);
    RUN_TEST(test_sequence_repeats_after_255_bytes);
    RUN_TEST(test_randomizing_twice_restores_the_frame);
    RUN_TEST(test_every_frame_restarts_from_the_seed);
    RUN_TEST(test_split_apply_continues_the_sequence);
    RUN_TEST(test_null_arguments_are_rejected);
    return UNITY_END();
}
