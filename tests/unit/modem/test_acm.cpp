/**
 * @file test_acm.cpp
 * @brief ACM controller: targets, immediate down-switch, held up-switch with hysteresis, lock
 *        loss, configuration changes, and a closed loop over the simulated link.
 *
 * @verifies SRS-ACM-001
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/acm/acm.h"
#include "satlink/amp_client/modem_client.hpp"
#include "satlink/amp_client/simulated_core1.hpp"
#include "satlink/modem/modcod.h"

namespace {

struct Change
{
    std::uint8_t from;
    std::uint8_t to;
    satlink_acm_reason_t reason;
};

class AcmTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto cfg = satlink_acm_default_config();
        ASSERT_EQ(SATLINK_OK, satlink_acm_init(&acm_, &cfg, &OnChange, this));
    }
    static void OnChange(void *ctx, std::uint8_t from, std::uint8_t to, satlink_acm_reason_t r,
                         float)
    {
        static_cast<AcmTest *>(ctx)->changes_.push_back({from, to, r});
    }
    std::uint8_t Run(float esn0, int frames, bool locked = true)
    {
        std::uint8_t m = 0;
        for (int i = 0; i < frames; ++i)
        {
            m = satlink_acm_update(&acm_, esn0, locked);
        }
        return m;
    }
    satlink_acm_t acm_{};
    std::vector<Change> changes_;
};

TEST_F(AcmTest, TargetFollowsThresholdsPlusMargin)
{
    // Thresholds 2, 5, 8, 11.5, 15 dB; margin 1 dB.
    EXPECT_EQ(0, satlink_acm_target(&acm_, -5.0F));
    EXPECT_EQ(0, satlink_acm_target(&acm_, 5.9F));
    EXPECT_EQ(1, satlink_acm_target(&acm_, 6.0F));
    EXPECT_EQ(2, satlink_acm_target(&acm_, 9.0F));
    EXPECT_EQ(3, satlink_acm_target(&acm_, 12.5F));
    EXPECT_EQ(4, satlink_acm_target(&acm_, 30.0F));
}

TEST_F(AcmTest, StepsUpOneAtATimeAfterTheHold)
{
    EXPECT_EQ(0, Run(30.0F, 2));
    EXPECT_EQ(1, Run(30.0F, 1)); // third good frame
    EXPECT_EQ(1, Run(30.0F, 2));
    EXPECT_EQ(2, Run(30.0F, 1));
    EXPECT_EQ(4, Run(30.0F, 6));
    ASSERT_EQ(4U, changes_.size());
    EXPECT_EQ(SATLINK_ACM_UP, changes_[0].reason);
    EXPECT_EQ(4U, acm_.changes_up);
}

TEST_F(AcmTest, HysteresisBlocksMarginalUpSwitch)
{
    // 6.5 dB supports MODCOD 1 (5 + 1) but not 1 + hysteresis (7 dB needed to step up to it).
    EXPECT_EQ(0, Run(6.5F, 20));
    EXPECT_EQ(1, Run(7.0F, 3));
}

TEST_F(AcmTest, DropsImmediatelyToTheTarget)
{
    Run(30.0F, 12);
    ASSERT_EQ(4, acm_.current);
    EXPECT_EQ(1, Run(6.2F, 1)); // fade: straight from 4 to 1
    EXPECT_EQ(SATLINK_ACM_DOWN, changes_.back().reason);
    EXPECT_EQ(4, changes_.back().from);
}

TEST_F(AcmTest, NoisyEstimateAroundABoundaryDoesNotOscillate)
{
    Run(30.0F, 12);
    Run(9.5F, 1); // settle at MODCOD 2 (needs 9 dB)
    const auto before = changes_.size();
    const float wobble[] = {9.4F, 10.3F, 9.2F, 10.6F, 9.8F, 10.4F, 9.1F, 10.9F};
    for (int i = 0; i < 50; ++i)
    {
        satlink_acm_update(&acm_, wobble[i % 8], true);
    }
    EXPECT_EQ(before, changes_.size());
    EXPECT_EQ(2, acm_.current);
}

TEST_F(AcmTest, LockLossFallsBackToMostRobust)
{
    Run(30.0F, 12);
    EXPECT_EQ(0, Run(30.0F, 1, false));
    EXPECT_EQ(SATLINK_ACM_LOCK_LOST, changes_.back().reason);
    EXPECT_EQ(0, Run(30.0F, 5, false));
    EXPECT_EQ(1, Run(30.0F, 3));
}

TEST_F(AcmTest, RangeLimitsAndReconfiguration)
{
    satlink_acm_config_t cfg = satlink_acm_default_config();
    cfg.min_modcod = 1;
    cfg.max_modcod = 2;
    ASSERT_EQ(SATLINK_OK, satlink_acm_configure(&acm_, &cfg));
    EXPECT_EQ(1, acm_.current); // clamped up into the range
    EXPECT_EQ(2, Run(30.0F, 20));
    EXPECT_EQ(1, Run(-3.0F, 1)); // never below min

    cfg.min_modcod = 3;
    cfg.max_modcod = 2;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_acm_configure(&acm_, &cfg));
    cfg = satlink_acm_default_config();
    cfg.max_modcod = 5;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_acm_configure(&acm_, &cfg));
    cfg = satlink_acm_default_config();
    cfg.up_hold = 0;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_acm_init(&acm_, &cfg, nullptr, nullptr));
    EXPECT_EQ(SATLINK_ERR_NULL, satlink_acm_init(nullptr, &cfg, nullptr, nullptr));
}

TEST(AcmClosedLoopTest, LinkClimbsToTheBestModcodTheChannelAllows)
{
    satlink::amp::SimulatedCore1 core1;
    satlink::amp::ModemClient client(core1);
    ASSERT_EQ(0, client.SetAcm({true, 0, 4, 100, 100}));
    // Noise sigma 0.25: Es/N0 about 12 dB, enough for 8PSK 2/3 (11.5 + 1 = 12.5 is not met,
    // QPSK 3/4 needs 9) -> the loop must settle on MODCOD 2.
    ASSERT_EQ(0, client.SetChannel(1024, 0x7FFF));
    core1.Advance(20000);
    client.Poll(std::chrono::milliseconds{0});
    ASSERT_TRUE(client.LastStatus().has_value());
    EXPECT_EQ(2, client.LastStatus().value_or(satlink_msg_status_t{}).tx_modcod);
    EXPECT_NEAR(12.0, client.LastStatus().value_or(satlink_msg_status_t{}).esn0_cdb / 100.0, 1.0);

    // Clear sky: climbs to the top.
    ASSERT_EQ(0, client.SetChannel(0, 0x7FFF));
    core1.Advance(15000);
    client.Poll(std::chrono::milliseconds{0});
    EXPECT_EQ(4, client.LastStatus().value_or(satlink_msg_status_t{}).tx_modcod);
    EXPECT_EQ(0U, client.LastStatus().value_or(satlink_msg_status_t{}).frames_crc_error);
}

} // namespace
