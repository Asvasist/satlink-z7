/**
 * @file test_ab_slot.c
 * @brief A/B boot-slot model: updates, trial boots, confirmation and rollback.
 *
 * @verifies SRS-BOOT-003
 */
#include <stdbool.h>
#include <stdint.h>

#include "satlink/boot/ab_slot.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_steady_state_always_boots_the_same_slot(void)
{
    satlink_ab_t ab;
    bool rolled_back = true;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_B, 3U));
    for (int i = 0; i < 10; ++i)
    {
        TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, &rolled_back));
        TEST_ASSERT_FALSE(rolled_back);
    }
    TEST_ASSERT_FALSE(ab.trial);
    TEST_ASSERT_EQUAL_UINT8(0U, ab.bootcount);
}

static void test_an_update_boots_the_other_slot_on_trial_and_can_be_confirmed(void)
{
    satlink_ab_t ab;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, 3U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));

    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, NULL));
    TEST_ASSERT_TRUE(ab.trial);
    TEST_ASSERT_EQUAL_UINT8(1U, ab.bootcount);

    satlink_ab_mark_good(&ab);
    TEST_ASSERT_FALSE(ab.trial);
    TEST_ASSERT_EQUAL_UINT8(0U, ab.bootcount);
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, NULL));
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, ab.previous); /* B is now the slot to fall back to */
}

static void test_a_slot_that_never_confirms_is_tried_bootlimit_times_and_then_abandoned(void)
{
    for (uint8_t limit = 1U; limit <= 5U; ++limit)
    {
        satlink_ab_t ab;
        bool rolled_back = false;
        unsigned trial_boots = 0U;

        TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, limit));
        TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));

        /* Power-cycle without ever confirming. */
        for (unsigned boot = 0U; boot < 20U; ++boot)
        {
            const satlink_slot_t slot = satlink_ab_select(&ab, &rolled_back);

            if (slot == SATLINK_SLOT_B)
            {
                trial_boots++;
            }
            else
            {
                TEST_ASSERT_TRUE(rolled_back);
                break;
            }
        }

        TEST_ASSERT_EQUAL_UINT(limit, trial_boots);
        TEST_ASSERT_EQUAL(SATLINK_SLOT_A, ab.active);
        TEST_ASSERT_FALSE(ab.trial);
        TEST_ASSERT_EQUAL_UINT8(0U, ab.bootcount);

        /* And it stays on A afterwards, with no further rollback events. */
        TEST_ASSERT_EQUAL(SATLINK_SLOT_A, satlink_ab_select(&ab, &rolled_back));
        TEST_ASSERT_FALSE(rolled_back);
    }
}

static void test_confirming_on_the_last_allowed_attempt_keeps_the_new_slot(void)
{
    satlink_ab_t ab;
    bool rolled_back = false;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, 3U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));

    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, &rolled_back)); /* attempt 1 */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, &rolled_back)); /* attempt 2 */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, &rolled_back)); /* attempt 3 */
    TEST_ASSERT_FALSE(rolled_back);
    satlink_ab_mark_good(&ab);

    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, &rolled_back));
    TEST_ASSERT_FALSE(rolled_back);
}

static void test_a_second_update_is_refused_while_the_first_is_on_trial(void)
{
    satlink_ab_t ab;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, 3U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));
    TEST_ASSERT_EQUAL(SATLINK_ERR_STATE, satlink_ab_begin_update(&ab));
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, ab.active); /* unchanged */

    satlink_ab_mark_good(&ab);
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab)); /* B -> A now */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_A, ab.active);
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, ab.previous);
}

static void test_after_a_rollback_a_new_update_starts_from_the_good_slot(void)
{
    satlink_ab_t ab;
    bool rolled_back = false;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, 1U));
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));
    (void)satlink_ab_select(&ab, &rolled_back);                              /* B, attempt 1 */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_A, satlink_ab_select(&ab, &rolled_back)); /* rolled back */
    TEST_ASSERT_TRUE(rolled_back);

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab)); /* try B again with a fix */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_ab_select(&ab, NULL));
}

static void test_the_running_slot_is_never_unconfirmed_for_longer_than_the_limit(void)
{
    /* Property: whatever happens after an update, the trial slot boots at most bootlimit
     * times in a row before the model returns to the last confirmed slot. */
    for (uint8_t limit = 1U; limit <= 4U; ++limit)
    {
        for (unsigned confirm_on = 0U; confirm_on <= (unsigned)limit + 2U; ++confirm_on)
        {
            satlink_ab_t ab;
            unsigned trial_boots = 0U;
            bool confirmed = false;

            TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_init(&ab, SATLINK_SLOT_A, limit));
            TEST_ASSERT_EQUAL(SATLINK_OK, satlink_ab_begin_update(&ab));

            for (unsigned boot = 0U; boot < 12U; ++boot)
            {
                const satlink_slot_t slot = satlink_ab_select(&ab, NULL);

                if ((slot == SATLINK_SLOT_B) && !confirmed)
                {
                    trial_boots++;
                    if (trial_boots == confirm_on)
                    {
                        satlink_ab_mark_good(&ab);
                        confirmed = true;
                    }
                }
            }

            if (confirm_on >= 1U && confirm_on <= limit)
            {
                TEST_ASSERT_TRUE(confirmed);
                TEST_ASSERT_EQUAL(SATLINK_SLOT_B, ab.active);
            }
            else
            {
                TEST_ASSERT_FALSE(confirmed);
                TEST_ASSERT_EQUAL_UINT(limit, trial_boots);
                TEST_ASSERT_EQUAL(SATLINK_SLOT_A, ab.active);
            }
        }
    }
}

static void test_init_and_null_handling(void)
{
    satlink_ab_t ab;
    bool rolled_back = true;

    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_ab_init(NULL, SATLINK_SLOT_A, 3U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_ab_init(&ab, SATLINK_SLOT_A, 0U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_ab_begin_update(NULL));
    TEST_ASSERT_EQUAL(SATLINK_SLOT_A, satlink_ab_select(NULL, &rolled_back));
    TEST_ASSERT_FALSE(rolled_back);
    satlink_ab_mark_good(NULL); /* must not crash */
    TEST_ASSERT_EQUAL(SATLINK_SLOT_B, satlink_slot_other(SATLINK_SLOT_A));
    TEST_ASSERT_EQUAL(SATLINK_SLOT_A, satlink_slot_other(SATLINK_SLOT_B));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_steady_state_always_boots_the_same_slot);
    RUN_TEST(test_an_update_boots_the_other_slot_on_trial_and_can_be_confirmed);
    RUN_TEST(test_a_slot_that_never_confirms_is_tried_bootlimit_times_and_then_abandoned);
    RUN_TEST(test_confirming_on_the_last_allowed_attempt_keeps_the_new_slot);
    RUN_TEST(test_a_second_update_is_refused_while_the_first_is_on_trial);
    RUN_TEST(test_after_a_rollback_a_new_update_starts_from_the_good_slot);
    RUN_TEST(test_the_running_slot_is_never_unconfirmed_for_longer_than_the_limit);
    RUN_TEST(test_init_and_null_handling);
    return UNITY_END();
}
