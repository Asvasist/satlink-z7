/**
 * @file test_hkc_app.cpp
 * @brief Housekeeping application logic against a fake board: command handling, periodic
 *        housekeeping, error latching, heartbeat and bootloader entry.
 *
 * @verifies SRS-HKC-005
 * @verifies SRS-HKC-003
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/hkc/hk_proto.h"

#include "hkc/hkc_app.h"

namespace satlink::test {
namespace {

struct FakeBoard
{
    static std::uint16_t ReadSensor(void *ctx, hkc_sensor_t sensor)
    {
        return static_cast<FakeBoard *>(ctx)->codes[sensor];
    }
    static bool Alarm(void *ctx)
    {
        return static_cast<FakeBoard *>(ctx)->alarm;
    }
    static std::uint8_t Switches(void *ctx)
    {
        return static_cast<FakeBoard *>(ctx)->switches;
    }
    static void SetRgb(void *ctx, std::uint8_t rgb)
    {
        static_cast<FakeBoard *>(ctx)->rgb.push_back(rgb);
    }
    static satlink_status_t Send(void *ctx, const satlink_can_frame_t *frame)
    {
        auto *self = static_cast<FakeBoard *>(ctx);
        if (self->fail_send)
        {
            return SATLINK_ERR_FULL;
        }
        self->sent.push_back(*frame);
        return SATLINK_OK;
    }
    static void EnterBoot(void *ctx)
    {
        ++static_cast<FakeBoard *>(ctx)->boot_entries;
    }

    hkc_app_hw_t Hw()
    {
        return {this, &ReadSensor, &Alarm, &Switches, &SetRgb, &Send, &EnterBoot};
    }

    std::uint16_t codes[7] = {2464, 1365, 2458, 1365, 1365, 2458, 1843};
    bool alarm = false;
    std::uint8_t switches = 0x5;
    bool fail_send = false;
    std::vector<std::uint8_t> rgb;
    std::vector<satlink_can_frame_t> sent;
    int boot_entries = 0;
};

class HkcAppTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const hkc_app_hw_t hw = board_.Hw();
        ASSERT_EQ(SATLINK_OK, hkc_app_init(&app_, &hw, SATLINK_HK_RESET_WATCHDOG));
    }

    satlink_hk_ack_t Command(std::uint8_t op, std::vector<std::uint8_t> args, std::uint32_t now = 0)
    {
        satlink_can_frame_t frame{};
        satlink_hk_encode_command(op, args.data(), static_cast<std::uint8_t>(args.size()), &frame);
        board_.sent.clear();
        hkc_app_on_frame(&app_, &frame, now);
        satlink_hk_ack_t ack{};
        EXPECT_EQ(1U, board_.sent.size());
        if (!board_.sent.empty())
        {
            EXPECT_EQ(SATLINK_OK, satlink_hk_decode_ack(&board_.sent.back(), &ack));
        }
        return ack;
    }

    FakeBoard board_;
    hkc_app_t app_{};
};

TEST_F(HkcAppTest, InitRejectsMissingCallbacks)
{
    hkc_app_hw_t hw = board_.Hw();
    hw.send = nullptr;
    hkc_app_t app{};
    EXPECT_EQ(SATLINK_ERR_NULL, hkc_app_init(&app, &hw, 0));
}

TEST_F(HkcAppTest, FirstPollSendsHousekeepingThenWaitsForPeriod)
{
    hkc_app_poll(&app_, 0);
    ASSERT_EQ(3U, board_.sent.size());
    satlink_hk_env_t env{};
    ASSERT_EQ(SATLINK_OK, satlink_hk_decode_env(&board_.sent[0], &env));
    EXPECT_NEAR(3002, env.die_temp_centi_c, 1);
    EXPECT_NEAR(1000, env.vccint_mv, 1);
    satlink_hk_supply_t supply{};
    ASSERT_EQ(SATLINK_OK, satlink_hk_decode_supply(&board_.sent[1], &supply));
    EXPECT_NEAR(1350, supply.vcco_ddr_mv, 1);
    satlink_hk_status_t status{};
    ASSERT_EQ(SATLINK_OK, satlink_hk_decode_status(&board_.sent[2], &status));
    EXPECT_EQ(SATLINK_HK_RESET_WATCHDOG, status.reset_cause);
    EXPECT_EQ(0x5, status.switches);

    board_.sent.clear();
    hkc_app_poll(&app_, 999);
    EXPECT_TRUE(board_.sent.empty());
    hkc_app_poll(&app_, 1000);
    EXPECT_EQ(3U, board_.sent.size());
}

TEST_F(HkcAppTest, SchedulingSurvivesMillisecondCounterWrap)
{
    hkc_app_poll(&app_, 0xFFFFFF00U);
    board_.sent.clear();
    hkc_app_poll(&app_, 0xFFFFFFF0U);
    EXPECT_TRUE(board_.sent.empty());
    hkc_app_poll(&app_, 0x000002E8U); // 0xFFFFFF00 + 1000, wrapped
    EXPECT_EQ(3U, board_.sent.size());
}

TEST_F(HkcAppTest, SetPeriodValidatesRange)
{
    EXPECT_EQ(SATLINK_HK_ACK_BAD_ARG, Command(SATLINK_HK_CMD_SET_PERIOD, {50, 0}).status);
    EXPECT_EQ(SATLINK_HK_ACK_BAD_ARG, Command(SATLINK_HK_CMD_SET_PERIOD, {0x11, 0x27}).status);
    EXPECT_EQ(SATLINK_HK_ACK_BAD_ARG, Command(SATLINK_HK_CMD_SET_PERIOD, {1}).status);
    EXPECT_EQ(SATLINK_HK_ACK_OK, Command(SATLINK_HK_CMD_SET_PERIOD, {0xF4, 0x01}, 10).status);
    EXPECT_EQ(500, app_.period_ms);

    board_.sent.clear();
    hkc_app_poll(&app_, 10);
    hkc_app_poll(&app_, 509);
    EXPECT_EQ(3U, board_.sent.size());
    hkc_app_poll(&app_, 510);
    EXPECT_EQ(6U, board_.sent.size());
}

TEST_F(HkcAppTest, VersionAndUnknownCommand)
{
    const auto ack = Command(SATLINK_HK_CMD_GET_VERSION, {});
    EXPECT_EQ(SATLINK_HK_ACK_OK, ack.status);
    ASSERT_EQ(3, ack.datac);
    EXPECT_EQ(HKC_APP_VERSION_MAJOR, ack.datav[0]);
    EXPECT_EQ(SATLINK_HK_ACK_UNKNOWN, Command(0x77, {}).status);
}

TEST_F(HkcAppTest, CommandCounterCountsOnlyAcceptedCommands)
{
    Command(SATLINK_HK_CMD_GET_VERSION, {});
    Command(0x77, {});
    Command(SATLINK_HK_CMD_CLEAR_ERRORS, {});
    EXPECT_EQ(2, app_.cmd_count);
}

TEST_F(HkcAppTest, EnterBootNeedsKeyAndHappensAfterTheAck)
{
    EXPECT_EQ(SATLINK_HK_ACK_BAD_ARG, Command(SATLINK_HK_CMD_ENTER_BOOT, {0xB0, 0x08}).status);
    EXPECT_EQ(SATLINK_HK_ACK_OK, Command(SATLINK_HK_CMD_ENTER_BOOT, {0xB0, 0x07}).status);
    EXPECT_EQ(0, board_.boot_entries); // the ACK is out first
    hkc_app_poll(&app_, 1);
    EXPECT_EQ(1, board_.boot_entries);
}

TEST_F(HkcAppTest, ErrorsLatchUntilCleared)
{
    board_.alarm = true;
    hkc_app_poll(&app_, 0);
    board_.alarm = false;
    board_.fail_send = true;
    hkc_app_poll(&app_, 1000);
    board_.fail_send = false;
    board_.sent.clear();
    hkc_app_poll(&app_, 2000);
    satlink_hk_status_t status{};
    ASSERT_EQ(SATLINK_OK, satlink_hk_decode_status(&board_.sent[2], &status));
    EXPECT_EQ(SATLINK_HK_ERR_XADC_ALARM | SATLINK_HK_ERR_CAN_TX, status.error_flags);

    Command(SATLINK_HK_CMD_CLEAR_ERRORS, {});
    EXPECT_EQ(0, app_.error_flags);
}

TEST_F(HkcAppTest, HeartbeatBlinksGreenOrRedAndStopsOnOverride)
{
    hkc_app_poll(&app_, 0);
    hkc_app_poll(&app_, 500);
    ASSERT_EQ(2U, board_.rgb.size());
    EXPECT_EQ(HKC_LED_GREEN, board_.rgb[0]);
    EXPECT_EQ(0, board_.rgb[1]);

    board_.alarm = true;
    hkc_app_poll(&app_, 1000);
    EXPECT_EQ(HKC_LED_RED, board_.rgb.back());

    Command(SATLINK_HK_CMD_SET_LED, {HKC_LED_BLUE});
    EXPECT_EQ(HKC_LED_BLUE, board_.rgb.back());
    const auto count = board_.rgb.size();
    hkc_app_poll(&app_, 5000);
    EXPECT_EQ(count, board_.rgb.size());
}

TEST_F(HkcAppTest, TimeSyncSetsUnixOffsetAndOtherFramesAreIgnored)
{
    satlink_hk_time_t time{1760000000U, 0};
    satlink_can_frame_t frame{};
    satlink_hk_encode_time(&time, &frame);
    hkc_app_on_frame(&app_, &frame, 5000);
    EXPECT_EQ(1760000000U - 5U, app_.unix_offset_s);
    EXPECT_TRUE(board_.sent.empty());

    frame.id = 0x555;
    hkc_app_on_frame(&app_, &frame, 0);
    EXPECT_TRUE(board_.sent.empty());
}

} // namespace
} // namespace satlink::test
