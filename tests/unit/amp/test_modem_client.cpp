/**
 * @file test_modem_client.cpp
 * @brief Linux modem client against the simulated Core 1 (the real modem application behind a
 *        MessagePort): commands, frames end to end, status, logs and ping.
 *
 * @verifies SRS-AMP-003
 * @verifies SRS-AMP-007
 */
#include <array>
#include <cerrno>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/amp_client/modem_client.hpp"
#include "satlink/amp_client/simulated_core1.hpp"

namespace satlink::amp {
namespace {

TEST(ModemClientTest, FramesStatusAndLogsFlowThroughTheSimulatedFirmware)
{
    SimulatedCore1 core1;
    ModemClient client(core1);
    std::vector<RxFrame> frames;
    std::vector<satlink_msg_status_t> statuses;
    std::vector<LogLine> logs;
    client.on_rx_frame = [&](const RxFrame &f) { frames.push_back(f); };
    client.on_status = [&](const satlink_msg_status_t &s) { statuses.push_back(s); };
    client.on_log = [&](const LogLine &l) { logs.push_back(l); };

    ASSERT_EQ(0, client.Configure({true, true, 3, SATLINK_LOOP_SOFTWARE}));
    core1.Advance(1500);
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        std::array<std::uint8_t, SATLINK_MSG_FRAME_BYTES> data{};
        data.fill(static_cast<std::uint8_t>(0x10 + i));
        ASSERT_EQ(0, client.SendFrame(data));
    }
    core1.Advance(2000);
    while (client.Poll(std::chrono::milliseconds{0}) > 0)
    {
    }

    ASSERT_EQ(3U, frames.size());
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        EXPECT_TRUE(frames[i].crc_ok);
        EXPECT_EQ(3, frames[i].modcod);
        EXPECT_EQ(0x10 + i, frames[i].data[64]);
        EXPECT_GT(frames[i].esn0_db, 20.0F);
    }
    ASSERT_GE(statuses.size(), 3U);
    EXPECT_EQ(1, statuses.back().locked);
    EXPECT_EQ(0U, statuses.back().bit_errors);
    ASSERT_TRUE(client.LastStatus().has_value());
    EXPECT_EQ(3000U, client.LastStatus().value_or(satlink_msg_status_t{}).uptime_ms);
    ASSERT_FALSE(logs.empty());
    EXPECT_EQ("modem configured", logs.front().text);
    EXPECT_EQ(0U, client.BadMessages());
}

TEST(ModemClientTest, ChannelNoiseLowersTheMeasuredSnr)
{
    SimulatedCore1 core1;
    ModemClient client(core1);
    ASSERT_EQ(0, client.Configure({true, true, 0, SATLINK_LOOP_SOFTWARE}));
    ASSERT_EQ(0, client.SetChannel(1300, 0x7FFF)); // sigma 0.317: Es/N0 about 10 dB
    core1.Advance(6000);
    client.Poll(std::chrono::milliseconds{0});
    ASSERT_TRUE(client.LastStatus().has_value());
    EXPECT_NEAR(1000, client.LastStatus().value_or(satlink_msg_status_t{}).esn0_cdb, 150);
}

TEST(ModemClientTest, PingAnsweredAfterTheFirmwareRuns)
{
    SimulatedCore1 core1;
    ModemClient client(core1);
    core1.Advance(250);
    // The simulated firmware only runs when advanced, so a ping with nobody advancing times out.
    EXPECT_FALSE(client.PingAndWait(1, std::chrono::milliseconds{20}).has_value());
    ASSERT_EQ(0, client.Ping(2));
    core1.Advance(10);
    std::uint32_t uptime = 0;
    client.on_pong = [&](const satlink_msg_ping_t &p) {
        if (p.token == 2)
        {
            uptime = p.uptime_ms;
        }
    };
    client.Poll(std::chrono::milliseconds{0});
    EXPECT_EQ(250U, uptime);
}

TEST(ModemClientTest, AcmConfigReachesTheFirmwareController)
{
    SimulatedCore1 core1;
    ModemClient client(core1);
    std::vector<LogLine> logs;
    client.on_log = [&](const LogLine &l) { logs.push_back(l); };
    ASSERT_EQ(0, client.SetAcm({true, 0, 4, 100, 50}));
    core1.Advance(4000);
    while (client.Poll(std::chrono::milliseconds{0}) > 0)
    {
    }
    ASSERT_TRUE(client.LastStatus().has_value());
    EXPECT_EQ(4, client.LastStatus().value_or(satlink_msg_status_t{}).tx_modcod);
    EXPECT_EQ(1, client.LastStatus().value_or(satlink_msg_status_t{}).acm_enabled);
    int acm_lines = 0;
    for (const auto &l : logs)
    {
        acm_lines += l.text.rfind("ACM ", 0) == 0 ? 1 : 0;
    }
    EXPECT_EQ(4, acm_lines); // ACM starts at the most robust MODCOD: four steps up
}

TEST(ModemClientTest, MalformedMessagesAreCounted)
{
    class Garbage : public MessagePort
    {
      public:
        int Send(std::uint16_t, std::span<const std::uint8_t>) override
        {
            return 0;
        }
        int Receive(Message &m, std::chrono::milliseconds) override
        {
            if (sent_)
            {
                return -ETIMEDOUT;
            }
            sent_ = true;
            m.type = SATLINK_MSG_STATUS;
            m.payload = {1, 2, 3};
            return 0;
        }
        bool sent_ = false;
    } port;
    ModemClient client(port);
    EXPECT_EQ(1, client.Poll(std::chrono::milliseconds{0}));
    EXPECT_EQ(1U, client.BadMessages());
    EXPECT_FALSE(client.LastStatus().has_value());
}

} // namespace
} // namespace satlink::amp
