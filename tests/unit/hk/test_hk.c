/**
 * @file test_hk.c
 * @brief Housekeeping library: XADC conversion, limits with hysteresis, watchdog supervisor and
 *        telemetry frames.
 *
 * @verifies SRS-HKC-001
 */
#include <stdbool.h>
#include <stdint.h>

#include "satlink/hk/limits.h"
#include "satlink/hk/supervisor.h"
#include "satlink/hk/telemetry.h"
#include "satlink/hk/xadc.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- XADC conversion ------------------------------------------------------------------- */

static void test_xadc_temperature_matches_the_datasheet_formula(void)
{
    /* code 0 is absolute zero, code 2545 (0x9F1) is about 40 degC, full scale about 230 degC. */
    TEST_ASSERT_EQUAL_INT32(-273150, satlink_xadc_temp_mdegc(0x0000U));
    TEST_ASSERT_INT32_WITHIN(10, 39989, satlink_xadc_temp_mdegc((uint16_t)(2545U << 4U)));
    TEST_ASSERT_INT32_WITHIN(5, 230702, satlink_xadc_temp_mdegc(0xFFF0U));
}

static void test_xadc_ignores_the_four_unused_low_bits(void)
{
    TEST_ASSERT_EQUAL_INT32(satlink_xadc_temp_mdegc(0x9F10U), satlink_xadc_temp_mdegc(0x9F1FU));
    TEST_ASSERT_EQUAL_UINT16(satlink_xadc_supply_mv(0x5550U), satlink_xadc_supply_mv(0x555FU));
}

static void test_xadc_supply_is_three_volts_at_full_scale(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, satlink_xadc_supply_mv(0x0000U));
    TEST_ASSERT_UINT16_WITHIN(1, 1000U, satlink_xadc_supply_mv((uint16_t)(1365U << 4U)));
    TEST_ASSERT_UINT16_WITHIN(1, 1800U, satlink_xadc_supply_mv((uint16_t)(2458U << 4U)));
    TEST_ASSERT_UINT16_WITHIN(1, 3000U, satlink_xadc_supply_mv(0xFFF0U));
}

/* ---- limits ------------------------------------------------------------------------------ */

static void test_a_high_limit_rises_at_once_and_falls_only_past_the_hysteresis(void)
{
    satlink_hk_limit_t limit;

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_HIGH, 70000, 85000, 3000));

    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(&limit, 69999));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 70000));
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 85000));

    /* Below 85000 but not below 82000: stays in alarm. */
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 83000));
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 82000));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 81999));

    /* Below 70000 but not below 67000: stays in warning. */
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 68000));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 67000));
    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(&limit, 66999));
}

static void test_a_reading_at_the_threshold_does_not_make_the_level_chatter(void)
{
    satlink_hk_limit_t limit;
    unsigned changes = 0U;
    satlink_hk_level_t last = SATLINK_HK_OK;

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_HIGH, 70000, 85000, 2000));
    for (int i = 0; i < 100; ++i)
    {
        const int32_t reading = ((i % 2) == 0) ? 70050 : 69950; /* jitter around the limit */
        const satlink_hk_level_t level = satlink_hk_limit_update(&limit, reading);

        if (level != last)
        {
            changes++;
            last = level;
        }
    }
    TEST_ASSERT_EQUAL_UINT(1U, changes); /* OK -> WARN once, then it holds */
}

static void test_a_fall_from_alarm_can_go_straight_to_ok(void)
{
    satlink_hk_limit_t limit;

    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_HIGH, 70000, 85000, 1000));
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 90000));
    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(&limit, 30000));
}

static void test_a_low_limit_works_the_other_way_round(void)
{
    satlink_hk_limit_t limit;

    /* A 1.0 V supply: warn at 950 mV, alarm at 900 mV, 20 mV hysteresis. */
    TEST_ASSERT_EQUAL(SATLINK_OK,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_LOW, 950, 900, 20));

    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(&limit, 1000));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 950));
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 899));
    TEST_ASSERT_EQUAL(SATLINK_HK_ALARM, satlink_hk_limit_update(&limit, 920));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 921));
    TEST_ASSERT_EQUAL(SATLINK_HK_WARN, satlink_hk_limit_update(&limit, 970));
    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(&limit, 971));
}

static void test_limit_init_rejects_misordered_thresholds_and_negative_hysteresis(void)
{
    satlink_hk_limit_t limit;

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_HIGH, 85000, 70000, 0));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_LOW, 900, 950, 0));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE,
                      satlink_hk_limit_init(&limit, SATLINK_HK_LIMIT_HIGH, 70000, 85000, -1));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL,
                      satlink_hk_limit_init(NULL, SATLINK_HK_LIMIT_HIGH, 70000, 85000, 0));
    TEST_ASSERT_EQUAL(SATLINK_HK_OK, satlink_hk_limit_update(NULL, 1));
}

/* ---- watchdog supervisor ------------------------------------------------------------------- */

static void test_supervisor_kicks_once_per_window_when_everyone_checked_in(void)
{
    satlink_wdg_sup_t sup;
    unsigned kicks = 0U;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_wdg_sup_init(&sup, 0x05U, 10U)); /* tasks 0 and 2 */

    for (unsigned window = 0U; window < 3U; ++window)
    {
        for (unsigned tick = 0U; tick < 10U; ++tick)
        {
            if (tick == 4U)
            {
                satlink_wdg_sup_checkin(&sup, 0U);
                satlink_wdg_sup_checkin(&sup, 2U);
            }
            if (satlink_wdg_sup_tick(&sup))
            {
                TEST_ASSERT_EQUAL_UINT(9U, tick); /* only when the window closes */
                kicks++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(3U, kicks);
    TEST_ASSERT_EQUAL_UINT32(3U, sup.kicks);
    TEST_ASSERT_EQUAL_UINT32(0U, sup.missed_windows);
}

static void test_supervisor_withholds_the_kick_and_names_the_missing_task(void)
{
    satlink_wdg_sup_t sup;
    bool kicked = false;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_wdg_sup_init(&sup, 0x07U, 5U)); /* tasks 0, 1, 2 */

    satlink_wdg_sup_checkin(&sup, 0U);
    satlink_wdg_sup_checkin(&sup, 2U); /* task 1 is hung */
    for (unsigned tick = 0U; tick < 5U; ++tick)
    {
        kicked = kicked || satlink_wdg_sup_tick(&sup);
    }

    TEST_ASSERT_FALSE(kicked);
    TEST_ASSERT_EQUAL_HEX32(0x02U, sup.last_missing);
    TEST_ASSERT_EQUAL_UINT32(1U, sup.missed_windows);
}

static void test_a_check_in_only_counts_for_its_own_window(void)
{
    satlink_wdg_sup_t sup;
    bool kicked = false;

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_wdg_sup_init(&sup, 0x01U, 4U));

    satlink_wdg_sup_checkin(&sup, 0U);
    for (unsigned tick = 0U; tick < 4U; ++tick)
    {
        kicked = satlink_wdg_sup_tick(&sup); /* window 1: alive */
    }
    TEST_ASSERT_TRUE(kicked);

    for (unsigned tick = 0U; tick < 4U; ++tick)
    {
        kicked = satlink_wdg_sup_tick(&sup); /* window 2: nobody checked in */
    }
    TEST_ASSERT_FALSE(kicked);
}

static void test_supervisor_ignores_bad_ids_and_validates_configuration(void)
{
    satlink_wdg_sup_t sup;

    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_wdg_sup_init(&sup, 0U, 10U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_wdg_sup_init(&sup, 1U, 0U));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_wdg_sup_init(NULL, 1U, 10U));

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_wdg_sup_init(&sup, 0x80000000U, 2U)); /* task 31 */
    satlink_wdg_sup_checkin(&sup, 32U);                                         /* out of range */
    satlink_wdg_sup_checkin(NULL, 0U);
    TEST_ASSERT_EQUAL_HEX32(0U, sup.alive_mask);
    satlink_wdg_sup_checkin(&sup, 31U);
    TEST_ASSERT_EQUAL_HEX32(0x80000000U, sup.alive_mask);
    TEST_ASSERT_FALSE(satlink_wdg_sup_tick(NULL));
}

/* ---- telemetry -------------------------------------------------------------------------- */

static satlink_hk_snapshot_t sample(void)
{
    const satlink_hk_snapshot_t s = {.temp_mdegc = 41250,
                                     .vccint_mv = 1002U,
                                     .vccaux_mv = 1803U,
                                     .vccbram_mv = 999U,
                                     .temp_level = SATLINK_HK_WARN,
                                     .vccint_level = SATLINK_HK_OK,
                                     .vccaux_level = SATLINK_HK_ALARM,
                                     .vccbram_level = SATLINK_HK_OK,
                                     .uptime_s = 0x01020304U,
                                     .watchdog_reset = true};
    return s;
}

static void test_a_snapshot_is_laid_out_as_documented(void)
{
    const satlink_hk_snapshot_t s = sample();
    satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT];

    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_pack(&s, 0x2AU, frames));

    TEST_ASSERT_EQUAL_HEX32(0x100U, frames[0].id);
    TEST_ASSERT_EQUAL_UINT8(8U, frames[0].dlc);
    {
        /* levels: temp 1, vccint 0, vccaux 2, vccbram 0 -> 0b00_10_00_01; 41250 = 0x0000A122 */
        const uint8_t expected[8] = {0x2AU, 0x21U, 0x22U, 0xA1U, 0x00U, 0x00U, 0x00U, 0x00U};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frames[0].data, 8U);
    }
    TEST_ASSERT_EQUAL_HEX32(0x101U, frames[1].id);
    {
        const uint8_t expected[8] = {0x2AU, 0x00U, 0xEAU, 0x03U, 0x0BU, 0x07U, 0xE7U, 0x03U};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frames[1].data, 8U);
    }
    TEST_ASSERT_EQUAL_HEX32(0x102U, frames[2].id);
    {
        const uint8_t expected[8] = {0x2AU, 0x01U, 0x04U, 0x03U, 0x02U, 0x01U, 0x00U, 0x00U};
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frames[2].data, 8U);
    }
}

static void test_a_snapshot_survives_a_round_trip_including_negative_temperature(void)
{
    satlink_hk_snapshot_t in = sample();
    satlink_hk_snapshot_t out = {0};
    satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT];
    uint8_t seq = 0U;

    in.temp_mdegc = -12345;
    TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_pack(&in, 7U, frames));
    for (size_t i = 0U; i < SATLINK_HK_FRAME_COUNT; ++i)
    {
        seq = 0U;
        TEST_ASSERT_EQUAL(SATLINK_OK, satlink_hk_decode(&frames[i], &out, &seq));
        TEST_ASSERT_EQUAL_UINT8(7U, seq);
    }

    TEST_ASSERT_EQUAL_INT32(-12345, out.temp_mdegc);
    TEST_ASSERT_EQUAL_UINT16(in.vccint_mv, out.vccint_mv);
    TEST_ASSERT_EQUAL_UINT16(in.vccaux_mv, out.vccaux_mv);
    TEST_ASSERT_EQUAL_UINT16(in.vccbram_mv, out.vccbram_mv);
    TEST_ASSERT_EQUAL(in.temp_level, out.temp_level);
    TEST_ASSERT_EQUAL(in.vccaux_level, out.vccaux_level);
    TEST_ASSERT_EQUAL_UINT32(in.uptime_s, out.uptime_s);
    TEST_ASSERT_TRUE(out.watchdog_reset);
}

static void test_decode_rejects_frames_that_are_not_housekeeping(void)
{
    satlink_hk_snapshot_t snap = sample();
    const satlink_hk_snapshot_t before = snap;
    satlink_can_frame_t f = {.id = 0x100U, .dlc = 8U};
    uint8_t seq = 0U;

    f.id = 0x103U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode(&f, &snap, &seq));
    f.id = 0x100U;
    f.dlc = 7U;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode(&f, &snap, &seq));
    f.dlc = 8U;
    f.extended = true;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode(&f, &snap, &seq));
    f.extended = false;
    f.rtr = true;
    TEST_ASSERT_EQUAL(SATLINK_ERR_RANGE, satlink_hk_decode(&f, &snap, &seq));

    TEST_ASSERT_EQUAL_INT32(before.temp_mdegc, snap.temp_mdegc);
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_hk_decode(NULL, &snap, &seq));
    TEST_ASSERT_EQUAL(SATLINK_ERR_NULL, satlink_hk_pack(NULL, 0U, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_xadc_temperature_matches_the_datasheet_formula);
    RUN_TEST(test_xadc_ignores_the_four_unused_low_bits);
    RUN_TEST(test_xadc_supply_is_three_volts_at_full_scale);
    RUN_TEST(test_a_high_limit_rises_at_once_and_falls_only_past_the_hysteresis);
    RUN_TEST(test_a_reading_at_the_threshold_does_not_make_the_level_chatter);
    RUN_TEST(test_a_fall_from_alarm_can_go_straight_to_ok);
    RUN_TEST(test_a_low_limit_works_the_other_way_round);
    RUN_TEST(test_limit_init_rejects_misordered_thresholds_and_negative_hysteresis);
    RUN_TEST(test_supervisor_kicks_once_per_window_when_everyone_checked_in);
    RUN_TEST(test_supervisor_withholds_the_kick_and_names_the_missing_task);
    RUN_TEST(test_a_check_in_only_counts_for_its_own_window);
    RUN_TEST(test_supervisor_ignores_bad_ids_and_validates_configuration);
    RUN_TEST(test_a_snapshot_is_laid_out_as_documented);
    RUN_TEST(test_a_snapshot_survives_a_round_trip_including_negative_temperature);
    RUN_TEST(test_decode_rejects_frames_that_are_not_housekeeping);
    return UNITY_END();
}
