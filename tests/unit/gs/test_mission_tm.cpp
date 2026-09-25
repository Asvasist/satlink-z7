/**
 * @file test_mission_tm.cpp
 * @brief Ground station core against the flight payload manager: every telecommand the ground
 *        station builds is accepted, every report the payload sends is decoded.
 *
 * @verifies SRS-GS-001
 */
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <type_traits>
#include <variant>

#include "../payload/payload_harness.hpp"
#include "mission_tm.hpp"

namespace satlink::test {
namespace {

using gs::Commander;
using payload::Event;
using payload::FailureCode;

struct Decoded
{
    std::vector<gs::ModemHk> modem;
    std::vector<gs::LinkHk> link;
    std::vector<gs::PlatformHk> platform;
    std::vector<gs::EventReport> events;
    std::vector<gs::VerificationReport> verification;
    int pings = 0;
    int unknown = 0;
};

Decoded DecodeAll(const FakeGround &g)
{
    Decoded d;
    for (const auto &tm : g.tm_)
    {
        std::visit(
            [&](const auto &r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, gs::ModemHk>)
                    d.modem.push_back(r);
                else if constexpr (std::is_same_v<T, gs::LinkHk>)
                    d.link.push_back(r);
                else if constexpr (std::is_same_v<T, gs::PlatformHk>)
                    d.platform.push_back(r);
                else if constexpr (std::is_same_v<T, gs::EventReport>)
                    d.events.push_back(r);
                else if constexpr (std::is_same_v<T, gs::VerificationReport>)
                    d.verification.push_back(r);
                else if constexpr (std::is_same_v<T, gs::PingReport>)
                    ++d.pings;
                else
                    ++d.unknown;
            },
            gs::Interpret(tm));
    }
    return d;
}

TEST(MissionTmTest, EveryCommandIsAcceptedAndCompleted)
{
    PayloadHarness h;
    Commander cmd;
    h.Run(2000);
    const std::array<std::uint8_t, 2> sids{1, 2};
    const std::array<std::uint8_t, 1> sid3{3};
    for (auto tc : {cmd.Ping(), cmd.SetModcod(2), cmd.SetAcm(true, 0, 3, 1.0, 0.5),
                    cmd.SetChannel(gs::NoiseLevelFor(20.0), 0x7FFF), cmd.StartPass(45.0, 20.0, 10),
                    cmd.StopPass(), cmd.SetLoopback(2), cmd.EnableHk(sids, true),
                    cmd.EnableHk(sid3, false), cmd.OneShotHk(sid3), cmd.SetHkPeriod(1, 500)})
    {
        h.ground.tc.push_back(tc);
    }
    EXPECT_EQ(11U, cmd.NextSequence());
    h.Run(6000);
    const Decoded d = DecodeAll(h.ground);
    EXPECT_EQ(0, d.unknown);
    EXPECT_EQ(1, d.pings);
    std::array<int, 9> by_subtype{};
    for (const auto &v : d.verification)
    {
        EXPECT_TRUE(v.Success()) << gs::FailureName(v.failure.value_or(FailureCode::kBadData));
        EXPECT_EQ(payload::kApidPayload, v.apid);
        ++by_subtype.at(v.subtype);
    }
    EXPECT_EQ(11, by_subtype[1]);
    EXPECT_EQ(11, by_subtype[7]);
    ASSERT_FALSE(d.modem.empty());
    ASSERT_FALSE(d.link.empty());
    ASSERT_FALSE(d.platform.empty());
    EXPECT_TRUE(d.modem.back().locked);
    EXPECT_TRUE(d.modem.back().acm);
    EXPECT_GT(d.modem.back().esn0_db, 15.0);
    EXPECT_GT(d.link.back().frames_received, 10U);
    EXPECT_FALSE(d.link.back().pass_active);
}

TEST(MissionTmTest, FailuresAndEventsAreDescribed)
{
    PayloadHarness h;
    Commander cmd;
    h.Run(2000);
    h.ground.tc.push_back(cmd.SetModcod(9));
    h.ground.tc.push_back(cmd.RestartModem());
    h.ground.tc.push_back(cmd.Build(42, 1));
    h.Run(2000);
    h.ground.tc.push_back(cmd.SetChannel(gs::NoiseLevelFor(3.0), 0x7FFF));
    h.Run(4000);
    h.ground.tc.push_back(cmd.SetChannel(0, 0x7FFF));
    h.Run(6000);
    const Decoded d = DecodeAll(h.ground);
    std::vector<FailureCode> failures;
    for (const auto &v : d.verification)
    {
        if (!v.Success() && v.failure.has_value())
        {
            failures.push_back(*v.failure);
        }
    }
    ASSERT_EQ(3U, failures.size());
    EXPECT_EQ(FailureCode::kBadData, failures[0]);
    EXPECT_EQ(FailureCode::kModemError, failures[1]);
    EXPECT_EQ(FailureCode::kUnknownService, failures[2]);
    EXPECT_EQ("bad application data", gs::FailureName(failures[0]));
    bool lost = false;
    for (const auto &e : d.events)
    {
        lost = lost || e.id == Event::kLinkLost;
    }
    EXPECT_TRUE(lost);
}

TEST(MissionTmTest, ConstellationOnRequest)
{
    PayloadHarness h;
    Commander cmd;
    h.Run(3000);
    EXPECT_TRUE(DecodeAll(h.ground).modem.size() > 0);
    const std::array<std::uint8_t, 1> sid4{4};
    ASSERT_TRUE(DecodeAll(h.ground).platform.size() > 0);
    h.ground.tc.push_back(cmd.SetModcod(3));
    h.ground.tc.push_back(cmd.EnableHk(sid4, true));
    h.Run(5000);
    std::vector<gs::ConstellationHk> points;
    for (const auto &tm : h.ground.tm_)
    {
        if (const auto r = gs::Interpret(tm); std::holds_alternative<gs::ConstellationHk>(r))
        {
            points.push_back(std::get<gs::ConstellationHk>(r));
        }
    }
    ASSERT_GE(points.size(), 3U);
    const auto &c = points.back();
    EXPECT_EQ(3, c.modcod);
    ASSERT_EQ(64U, c.points.size());
    for (const auto &[i, q] : c.points)
    {
        EXPECT_NEAR(1.0, std::hypot(i, q), 0.15); // 8PSK: on the unit circle
    }
    EXPECT_FALSE(gs::DecodeConstellationHk(std::vector<std::uint8_t>{4, 0, 1, 0}).has_value());
}

TEST(MissionTmTest, Decoders)
{
    EXPECT_FALSE(gs::DecodeModemHk(std::vector<std::uint8_t>{1, 0}).has_value());
    EXPECT_FALSE(gs::DecodeLinkHk(std::vector<std::uint8_t>{1}).has_value());
    EXPECT_FALSE(gs::DecodePlatformHk(std::vector<std::uint8_t>(20, 0)).has_value());
    std::vector<std::uint8_t> platform(20, 0);
    platform[0] = 3;
    platform[1] = 1;
    platform[2] = 0x0B; // 30.00 C
    platform[3] = 0xB8;
    const auto p = gs::DecodePlatformHk(platform);
    ASSERT_TRUE(p.has_value());
    EXPECT_DOUBLE_EQ(30.0, p.value_or(gs::PlatformHk{}).temperature_c);
    platform.push_back(0);
    EXPECT_FALSE(gs::DecodePlatformHk(platform).has_value());

    pus::Telemetry tm;
    tm.service = 3;
    tm.subtype = 25;
    tm.data = {9};
    EXPECT_TRUE(std::holds_alternative<gs::UnknownReport>(gs::Interpret(tm)));
    tm.data = {1, 2};
    EXPECT_TRUE(std::holds_alternative<gs::UnknownReport>(gs::Interpret(tm)));
    tm.data = {2};
    EXPECT_TRUE(std::holds_alternative<gs::UnknownReport>(gs::Interpret(tm)));
    tm.data = {3};
    EXPECT_TRUE(std::holds_alternative<gs::UnknownReport>(gs::Interpret(tm)));
    tm.service = 5;
    tm.subtype = 1;
    tm.data = {0, 3, 1, 2};
    const auto e = std::get<gs::EventReport>(gs::Interpret(tm));
    EXPECT_EQ("MODCOD changed: QPSK 1/2 -> QPSK 3/4", e.Describe());
    tm.data = {0, 6, 'h', 'i'};
    EXPECT_EQ("Firmware: hi", std::get<gs::EventReport>(gs::Interpret(tm)).Describe());
    tm.data = {0, 99};
    EXPECT_EQ("Event 99", std::get<gs::EventReport>(gs::Interpret(tm)).Describe());
    tm.service = 99;
    EXPECT_TRUE(std::holds_alternative<gs::UnknownReport>(gs::Interpret(tm)));

    for (unsigned i = 1; i <= 7; ++i)
    {
        EXPECT_NE(std::string::npos, gs::EventName(static_cast<Event>(i)).find_first_not_of(' '));
        EXPECT_EQ(std::string::npos, gs::FailureName(static_cast<FailureCode>(i)).find("failure"));
    }
    EXPECT_EQ("failure 77", gs::FailureName(static_cast<FailureCode>(77)));
    EXPECT_EQ("MODCOD 7", gs::ModcodName(7));
    EXPECT_EQ(0, gs::NoiseLevelFor(200.0));
    EXPECT_EQ(4096, gs::NoiseLevelFor(0.0));
}

} // namespace
} // namespace satlink::test
