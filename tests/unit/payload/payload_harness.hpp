/**
 * @file payload_harness.hpp
 * @brief Test harness: the payload manager wired to the simulated Core 1, a fake ground link,
 *        fake tunnels and a manual clock.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "satlink/amp_client/simulated_core1.hpp"
#include "satlink/payload/payload_manager.hpp"
#include "satlink/pus/space_packet.hpp"

namespace satlink::test {

class FakeClock : public payload::Clock
{
  public:
    std::uint64_t NowMs() override
    {
        return now;
    }
    std::uint64_t UnixMs() override
    {
        return 1760000000000ULL + now;
    }
    std::uint64_t now = 1000;
};

class FakeGround : public payload::GroundLink
{
  public:
    void SendTm(std::span<const std::uint8_t> packet) override
    {
        pus::Telemetry tm;
        if (pus::Decode(packet, tm) == pus::DecodeError::kNone)
        {
            tm_.push_back(tm);
        }
        else
        {
            ++undecodable;
        }
    }
    std::optional<std::vector<std::uint8_t>> ReceiveTc() override
    {
        if (tc.empty())
        {
            return std::nullopt;
        }
        auto p = std::move(tc.front());
        tc.pop_front();
        return p;
    }

    std::vector<pus::Telemetry> Find(std::uint8_t service, std::uint8_t subtype) const
    {
        std::vector<pus::Telemetry> out;
        for (const auto &tm : tm_)
        {
            if (tm.service == service && tm.subtype == subtype)
            {
                out.push_back(tm);
            }
        }
        return out;
    }

    std::deque<std::vector<std::uint8_t>> tc;
    std::vector<pus::Telemetry> tm_;
    int undecodable = 0;
};

class FakeTunnel : public payload::PacketTunnel
{
  public:
    std::optional<std::vector<std::uint8_t>> Read() override
    {
        if (outgoing.empty())
        {
            return std::nullopt;
        }
        auto p = std::move(outgoing.front());
        outgoing.pop_front();
        return p;
    }
    void Write(std::span<const std::uint8_t> packet) override
    {
        incoming.emplace_back(packet.begin(), packet.end());
    }
    std::deque<std::vector<std::uint8_t>> outgoing;
    std::vector<std::vector<std::uint8_t>> incoming;
};

struct PayloadHarness
{
    explicit PayloadHarness(payload::PayloadConfig cfg = {})
        : manager(std::move(cfg), core1, ground, clock, &sat, &gnd)
    {
        manager.Start();
    }

    void Run(std::uint32_t ms)
    {
        for (std::uint32_t t = 0; t < ms; t += 10)
        {
            clock.now += 10;
            core1.Advance(10);
            manager.Step();
        }
    }

    void SendTc(std::uint8_t service, std::uint8_t subtype, std::vector<std::uint8_t> data = {},
                std::uint16_t apid = payload::kApidPayload)
    {
        pus::Telecommand tc;
        tc.apid = apid;
        tc.sequence_count = seq++;
        tc.service = service;
        tc.subtype = subtype;
        tc.source_id = payload::kGroundId;
        tc.data = std::move(data);
        ground.tc.push_back(pus::Encode(tc));
    }

    amp::SimulatedCore1 core1;
    FakeGround ground;
    FakeClock clock;
    FakeTunnel sat;
    FakeTunnel gnd;
    payload::PayloadManager manager;
    std::uint16_t seq = 0;
};

} // namespace satlink::test
