/**
 * @file test_payload_manager.cpp
 * @brief Payload manager end to end: telecommands over the ground link, telemetry through the
 *        simulated modem link, PUS services, IP over the link, pass emulation.
 *
 * @verifies SRS-PLM-001
 * @verifies SRS-PLM-002
 * @verifies SRS-NET-001
 * @verifies SRS-PUS-003
 */
#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "payload_harness.hpp"

namespace satlink::test {
namespace {

using payload::Event;
using payload::FailureCode;
using payload::Function;

std::uint16_t U16(const std::vector<std::uint8_t> &d, std::size_t off)
{
    return static_cast<std::uint16_t>((d[off] << 8) | d[off + 1]);
}

std::uint32_t U32(const std::vector<std::uint8_t> &d, std::size_t off)
{
    return (static_cast<std::uint32_t>(U16(d, off)) << 16) | U16(d, off + 2);
}

bool HasEvent(const FakeGround &g, Event id)
{
    for (std::uint8_t sev = 1; sev <= 4; ++sev)
    {
        for (const auto &tm : g.Find(5, sev))
        {
            if (U16(tm.data, 0) == static_cast<std::uint16_t>(id))
            {
                return true;
            }
        }
    }
    return false;
}

TEST(PayloadManagerTest, AreYouAliveIsVerifiedAndAnsweredThroughTheRfLink)
{
    PayloadHarness h;
    h.Run(2000); // link acquisition
    h.SendTc(17, 1);
    h.Run(3000);
    ASSERT_EQ(1U, h.ground.Find(1, 1).size());
    ASSERT_EQ(1U, h.ground.Find(17, 2).size());
    ASSERT_EQ(1U, h.ground.Find(1, 7).size());
    const auto acc = h.ground.Find(1, 1)[0];
    EXPECT_EQ(0x1810, U16(acc.data, 0)); // TC packet ID of APID 0x010
    EXPECT_EQ(0xC000, U16(acc.data, 2)); // sequence 0
    EXPECT_EQ(0, h.ground.undecodable);
    EXPECT_GT(h.manager.Stats().tm_delivered, 3U);
    EXPECT_EQ(h.manager.Stats().tm_delivered, h.ground.tm_.size());
}

TEST(PayloadManagerTest, BadTelecommandsAreRejectedWithReasons)
{
    PayloadHarness h;
    h.Run(2000);
    auto corrupt = pus::Encode(pus::Telecommand{payload::kApidPayload, 9, 9, 17, 1, 1, {}});
    corrupt.back() ^= 0xFF;
    h.ground.tc.push_back(corrupt);
    h.SendTc(17, 1, {}, 0x055);                                           // wrong APID
    h.SendTc(99, 1);                                                      // unknown service
    h.SendTc(8, 1, {0x42});                                               // unknown function
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetModcod), 9}); // bad MODCOD
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kRestartModem)}); // no restart hook
    h.Run(4000);
    const auto acceptance = h.ground.Find(1, 2);
    const auto completion = h.ground.Find(1, 8);
    ASSERT_EQ(3U, acceptance.size());
    ASSERT_EQ(3U, completion.size());
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kCorruptPacket), U16(acceptance[0].data, 4));
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kBadApid), U16(acceptance[1].data, 4));
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kUnknownService), U16(acceptance[2].data, 4));
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kUnknownFunction),
              U16(completion[0].data, 4));
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kBadData), U16(completion[1].data, 4));
    EXPECT_EQ(static_cast<std::uint16_t>(FailureCode::kModemError), U16(completion[2].data, 4));
    EXPECT_EQ(6U, h.manager.Stats().tc_rejected);
}

TEST(PayloadManagerTest, PeriodicHousekeepingDescribesTheLink)
{
    PayloadHarness h;
    h.Run(8000);
    const auto modem = h.ground.Find(3, 25);
    ASSERT_GE(modem.size(), 12U); // 3 structures per second, minus the first seconds' losses
    const pus::Telemetry *sid1 = nullptr;
    const pus::Telemetry *sid2 = nullptr;
    for (const auto &tm : modem)
    {
        if (tm.data[0] == 1)
        {
            sid1 = &tm;
        }
        if (tm.data[0] == 2)
        {
            sid2 = &tm;
        }
    }
    ASSERT_NE(nullptr, sid1);
    ASSERT_NE(nullptr, sid2);
    EXPECT_EQ(1, sid1->data[1]);                                    // locked
    EXPECT_EQ(1, sid1->data[3]);                                    // ACM on
    EXPECT_GT(static_cast<std::int16_t>(U16(sid1->data, 4)), 2500); // Es/N0 > 25 dB
    EXPECT_GT(U32(sid1->data, 6), 10U);                             // frames ok
    EXPECT_EQ(0U, U32(sid1->data, 18));                             // bit errors
    EXPECT_GT(U32(sid2->data, 12), 5U); // frames received through the link
}

TEST(PayloadManagerTest, HousekeepingCanBeDisabledRequestedAndRetimed)
{
    PayloadHarness h;
    h.Run(2000);
    h.SendTc(3, 6, {3, 1, 2, 3}); // disable all three
    h.Run(3000);
    const std::size_t before = h.ground.Find(3, 25).size();
    h.Run(3000);
    EXPECT_EQ(before, h.ground.Find(3, 25).size());

    h.SendTc(3, 27, {1, 2}); // one-shot SID 2
    h.Run(2000);
    ASSERT_EQ(before + 1, h.ground.Find(3, 25).size());
    EXPECT_EQ(2, h.ground.Find(3, 25).back().data[0]);

    h.SendTc(3, 31, {1, 0, 0, 0x01, 0xF4}); // SID 1 every 500 ms
    h.SendTc(3, 5, {1, 1});
    h.Run(4000);
    const auto all = h.ground.Find(3, 25);
    const auto sid1 =
        std::count_if(all.begin(), all.end(), [](const auto &tm) { return tm.data[0] == 1; });
    EXPECT_GE(sid1, 5);
    h.SendTc(3, 31, {1, 0, 0, 0, 10}); // 10 ms: out of range
    h.Run(2000);
    EXPECT_EQ(1U, h.ground.Find(1, 8).size());
}

TEST(PayloadManagerTest, FunctionsReachTheModem)
{
    PayloadHarness h;
    h.Run(2000);
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetModcod), 3});
    h.Run(4000);
    EXPECT_EQ(3, h.manager.ModemStatus().value_or(satlink_msg_status_t{}).tx_modcod);
    EXPECT_EQ(0, h.manager.ModemStatus().value_or(satlink_msg_status_t{}).acm_enabled);

    // Es/N0 about 6 dB, far too little for 8PSK 2/3: the link is lost until ACM is back on.
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetChannel), 0x08, 0x00, 0x7F, 0xFF});
    h.Run(4000);
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetAcm), 1, 0, 4, 0, 100, 0, 100});
    h.Run(15000);
    EXPECT_TRUE(HasEvent(h.ground, Event::kLinkLost));
    EXPECT_TRUE(HasEvent(h.ground, Event::kLinkLocked));
    EXPECT_TRUE(HasEvent(h.ground, Event::kModcodChanged));
    EXPECT_EQ(1, h.manager.ModemStatus().value_or(satlink_msg_status_t{}).locked);
}

TEST(PayloadManagerTest, ReportsOfALinkChangeAreNotSentIntoItBlind)
{
    PayloadHarness h;
    h.Run(8000); // clean channel: ACM at 8PSK 5/6
    ASSERT_EQ(4, h.manager.ModemStatus().value_or(satlink_msg_status_t{}).tx_modcod);
    // 9 dB: far too little for 8PSK 5/6. The reports of this TC must wait for the new link.
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetChannel), 0x05, 0x0F, 0x7F, 0xFF});
    h.Run(10000);
    ASSERT_EQ(1U, h.ground.Find(1, 1).size());
    ASSERT_EQ(1U, h.ground.Find(1, 7).size());
    EXPECT_EQ(2, h.manager.ModemStatus().value_or(satlink_msg_status_t{}).tx_modcod);
}

TEST(PayloadManagerTest, IpDatagramsCrossTheLinkBothWays)
{
    PayloadHarness h;
    h.Run(2000);
    std::vector<std::uint8_t> down(300);
    std::vector<std::uint8_t> up(60);
    for (std::size_t i = 0; i < down.size(); ++i)
    {
        down[i] = static_cast<std::uint8_t>(i);
    }
    std::fill(up.begin(), up.end(), 0x7E);
    h.sat.outgoing.push_back(down);
    h.gnd.outgoing.push_back(up);
    h.Run(4000);
    ASSERT_EQ(1U, h.gnd.incoming.size());
    EXPECT_EQ(down, h.gnd.incoming[0]);
    ASSERT_EQ(1U, h.sat.incoming.size());
    EXPECT_EQ(up, h.sat.incoming[0]);
    EXPECT_EQ(1U, h.manager.Stats().ip_down);
    EXPECT_EQ(1U, h.manager.Stats().ip_up);
}

TEST(PayloadManagerTest, LeoPassDrivesAcmFromAosToLos)
{
    payload::PayloadConfig cfg;
    PayloadHarness h(cfg);
    h.Run(1000);
    // 80-degree pass, 20 dB at zenith, 25x time lapse: about 25 s of link time.
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kStartPass), 0x1F, 0x40, 0x07, 0xD0, 25});
    h.Run(2000);
    EXPECT_TRUE(h.manager.Pass().active);
    h.Run(30000);
    EXPECT_FALSE(h.manager.Pass().active);
    EXPECT_TRUE(HasEvent(h.ground, Event::kAos));
    EXPECT_TRUE(HasEvent(h.ground, Event::kModcodChanged));
    // LOS cannot be downlinked while the link is closed; it arrives once the pass has ended and
    // the channel is clear again.
    h.Run(5000);
    EXPECT_TRUE(HasEvent(h.ground, Event::kLos));
    // During the pass the link report carried the geometry.
    bool saw_elevation = false;
    for (const auto &tm : h.ground.Find(3, 25))
    {
        if (tm.data[0] == 2 && tm.data[1] == 1 && static_cast<std::int16_t>(U16(tm.data, 2)) > 4000)
        {
            saw_elevation = true;
        }
    }
    EXPECT_TRUE(saw_elevation);
}

TEST(PayloadManagerTest, DirectTelemetryDoesNotNeedTheLink)
{
    payload::PayloadConfig cfg;
    cfg.tm_direct = true;
    PayloadHarness h(cfg);
    h.SendTc(8, 1, {static_cast<std::uint8_t>(Function::kSetChannel), 0xFF, 0xFF, 0, 0});
    h.SendTc(17, 1);
    h.Run(500);
    EXPECT_EQ(1U, h.ground.Find(17, 2).size()); // straight to the ground, link or not
}

} // namespace
} // namespace satlink::test
