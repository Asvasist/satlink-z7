/**
 * @file mission_tm.cpp
 * @implements SRS-GS-001
 */
#include "mission_tm.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace satlink::gs {
namespace {

using payload::Event;
using payload::FailureCode;
using payload::HkStructure;

/// Big-endian reader that remembers whether it ran past the end.
class Reader
{
  public:
    explicit Reader(std::span<const std::uint8_t> d) : d_(d) {}

    std::uint8_t U8()
    {
        if (pos_ + 1 > d_.size())
        {
            ok_ = false;
            return 0;
        }
        return d_[pos_++];
    }
    std::uint16_t U16()
    {
        const auto hi = static_cast<unsigned>(U8());
        const auto lo = static_cast<unsigned>(U8());
        return static_cast<std::uint16_t>((hi << 8U) | lo);
    }
    std::int16_t S16()
    {
        return static_cast<std::int16_t>(U16());
    }
    std::uint32_t U32()
    {
        const std::uint32_t hi = U16();
        return (hi << 16U) | U16();
    }
    /// True when everything was read and nothing is left over.
    [[nodiscard]] bool Complete() const
    {
        return ok_ && pos_ == d_.size();
    }

  private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

void Put16(std::vector<std::uint8_t> &v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 8U));
    v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
}

std::uint16_t Centi(double v)
{
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 100.0)));
}

} // namespace

std::optional<ModemHk> DecodeModemHk(std::span<const std::uint8_t> data)
{
    Reader r(data);
    ModemHk h;
    if (r.U8() != static_cast<std::uint8_t>(HkStructure::kModem))
    {
        return std::nullopt;
    }
    h.locked = r.U8() != 0;
    h.modcod = r.U8();
    h.acm = r.U8() != 0;
    h.esn0_db = r.S16() / 100.0;
    h.frames_ok = r.U32();
    h.frames_crc_error = r.U32();
    h.header_errors = r.U32();
    h.bit_errors = r.U32();
    h.bits_checked = r.U32();
    h.latency_max_us = r.U32();
    h.latency_avg_us = r.U32();
    h.cpu_load_pct = r.U16() / 10.0;
    h.tx_queue = r.U8();
    h.uptime_ms = r.U32();
    return r.Complete() ? std::optional<ModemHk>(h) : std::nullopt;
}

std::optional<LinkHk> DecodeLinkHk(std::span<const std::uint8_t> data)
{
    Reader r(data);
    LinkHk h;
    if (r.U8() != static_cast<std::uint8_t>(HkStructure::kLink))
    {
        return std::nullopt;
    }
    h.pass_active = r.U8() != 0;
    h.elevation_deg = r.S16() / 100.0;
    h.range_km = r.U16();
    h.range_rate_km_s = r.S16() / 1000.0;
    h.pass_esn0_db = r.S16() / 100.0;
    h.frames_sent = r.U32();
    h.frames_received = r.U32();
    h.lost_frames = r.U32();
    h.packets = r.U32();
    h.resyncs = r.U32();
    h.ip_down = r.U32();
    h.ip_up = r.U32();
    h.dropped = r.U32();
    return r.Complete() ? std::optional<LinkHk>(h) : std::nullopt;
}

std::optional<PlatformHk> DecodePlatformHk(std::span<const std::uint8_t> data)
{
    Reader r(data);
    PlatformHk h;
    if (r.U8() != static_cast<std::uint8_t>(HkStructure::kPlatform))
    {
        return std::nullopt;
    }
    h.hkc_valid = r.U8() != 0;
    h.temperature_c = r.S16() / 100.0;
    h.vccint_mv = r.U16();
    h.vccaux_mv = r.U16();
    h.vbram_mv = r.U16();
    h.hkc_uptime_s = r.U32();
    h.hkc_error_flags = r.U8();
    h.rtos_state = r.U8();
    h.rtos_restarts = r.U32();
    return r.Complete() ? std::optional<PlatformHk>(h) : std::nullopt;
}

std::optional<ConstellationHk> DecodeConstellationHk(std::span<const std::uint8_t> data)
{
    Reader r(data);
    ConstellationHk h;
    if (r.U8() != static_cast<std::uint8_t>(HkStructure::kConstellation))
    {
        return std::nullopt;
    }
    h.modcod = r.U8();
    const std::uint8_t count = r.U8();
    for (std::uint8_t k = 0; k < count; ++k)
    {
        const auto i = static_cast<std::int8_t>(r.U8());
        const auto q = static_cast<std::int8_t>(r.U8());
        h.points.emplace_back(i / 64.0, q / 64.0);
    }
    return r.Complete() ? std::optional<ConstellationHk>(std::move(h)) : std::nullopt;
}

Report Interpret(const pus::Telemetry &tm)
{
    const std::span<const std::uint8_t> d(tm.data);
    if (tm.service == 3 && tm.subtype == 25 && !d.empty())
    {
        switch (static_cast<HkStructure>(d[0]))
        {
        case HkStructure::kModem:
            if (auto h = DecodeModemHk(d))
            {
                return *h;
            }
            break;
        case HkStructure::kLink:
            if (auto h = DecodeLinkHk(d))
            {
                return *h;
            }
            break;
        case HkStructure::kPlatform:
            if (auto h = DecodePlatformHk(d))
            {
                return *h;
            }
            break;
        case HkStructure::kConstellation:
            if (auto h = DecodeConstellationHk(d))
            {
                return *h;
            }
            break;
        }
        return UnknownReport{};
    }
    if (tm.service == 5 && tm.subtype >= 1 && tm.subtype <= 4 && d.size() >= 2)
    {
        EventReport e;
        e.severity = tm.subtype;
        e.id = static_cast<Event>((d[0] << 8) | d[1]);
        e.aux.assign(d.begin() + 2, d.end());
        return e;
    }
    if (tm.service == 1 && (d.size() == 4 || d.size() == 6))
    {
        VerificationReport v;
        v.subtype = tm.subtype;
        v.apid = static_cast<std::uint16_t>(((d[0] << 8) | d[1]) & 0x7FF);
        v.sequence_count = static_cast<std::uint16_t>(((d[2] << 8) | d[3]) & 0x3FFF);
        if (d.size() == 6)
        {
            v.failure = static_cast<FailureCode>((d[4] << 8) | d[5]);
        }
        return v;
    }
    if (tm.service == 17 && tm.subtype == 2)
    {
        return PingReport{};
    }
    return UnknownReport{};
}

std::string EventName(Event id)
{
    switch (id)
    {
    case Event::kLinkLocked:
        return "Link locked";
    case Event::kLinkLost:
        return "Link lost";
    case Event::kModcodChanged:
        return "MODCOD changed";
    case Event::kAos:
        return "AOS";
    case Event::kLos:
        return "LOS";
    case Event::kFirmwareLog:
        return "Firmware";
    case Event::kModemRestarted:
        return "Modem restarted";
    }
    return "Event " + std::to_string(static_cast<unsigned>(id));
}

std::string FailureName(FailureCode code)
{
    switch (code)
    {
    case FailureCode::kUnknownService:
        return "unknown service";
    case FailureCode::kUnknownSubtype:
        return "unknown subtype";
    case FailureCode::kBadData:
        return "bad application data";
    case FailureCode::kUnknownFunction:
        return "unknown function";
    case FailureCode::kModemError:
        return "modem error";
    case FailureCode::kBadApid:
        return "wrong APID";
    case FailureCode::kCorruptPacket:
        return "corrupt packet";
    }
    return "failure " + std::to_string(static_cast<unsigned>(code));
}

std::string ModcodName(std::uint8_t modcod)
{
    static constexpr const char *kNames[] = {"BPSK 1/2", "QPSK 1/2", "QPSK 3/4", "8PSK 2/3",
                                             "8PSK 5/6"};
    return modcod < std::size(kNames) ? kNames[modcod] : "MODCOD " + std::to_string(modcod);
}

std::string EventReport::Describe() const
{
    std::string text = EventName(id);
    if (id == Event::kModcodChanged && aux.size() == 2)
    {
        text += ": " + ModcodName(aux[0]) + " -> " + ModcodName(aux[1]);
    }
    else if (id == Event::kFirmwareLog && !aux.empty())
    {
        text += ": " + std::string(aux.begin(), aux.end());
    }
    return text;
}

std::uint16_t NoiseLevelFor(double esn0_db, double noise_scale)
{
    const double sigma = std::pow(10.0, -esn0_db / 20.0);
    return static_cast<std::uint16_t>(std::lround(std::clamp(sigma * noise_scale, 0.0, 65535.0)));
}

// ---- Commander ----

std::vector<std::uint8_t> Commander::Build(std::uint8_t service, std::uint8_t subtype,
                                           std::vector<std::uint8_t> data)
{
    pus::Telecommand tc;
    tc.apid = apid_;
    tc.sequence_count = seq_.Next();
    tc.ack_flags = pus::kAckAcceptance | pus::kAckCompletion;
    tc.service = service;
    tc.subtype = subtype;
    tc.source_id = payload::kGroundId;
    tc.data = std::move(data);
    return pus::Encode(tc);
}

std::vector<std::uint8_t> Commander::Function(payload::Function f, std::vector<std::uint8_t> args)
{
    args.insert(args.begin(), static_cast<std::uint8_t>(f));
    return Build(8, 1, std::move(args));
}

std::vector<std::uint8_t> Commander::SetModcod(std::uint8_t modcod)
{
    return Function(payload::Function::kSetModcod, {modcod});
}

std::vector<std::uint8_t> Commander::SetAcm(bool enabled, std::uint8_t min_modcod,
                                            std::uint8_t max_modcod, double margin_db,
                                            double hysteresis_db)
{
    std::vector<std::uint8_t> a{static_cast<std::uint8_t>(enabled ? 1 : 0), min_modcod, max_modcod};
    Put16(a, Centi(margin_db));
    Put16(a, Centi(hysteresis_db));
    return Function(payload::Function::kSetAcm, std::move(a));
}

std::vector<std::uint8_t> Commander::SetChannel(std::uint16_t noise_level, std::uint16_t gain_q15)
{
    std::vector<std::uint8_t> a;
    Put16(a, noise_level);
    Put16(a, gain_q15);
    return Function(payload::Function::kSetChannel, std::move(a));
}

std::vector<std::uint8_t> Commander::StartPass(double max_elevation_deg, double zenith_esn0_db,
                                               std::uint8_t time_scale)
{
    std::vector<std::uint8_t> a;
    Put16(a, Centi(max_elevation_deg));
    Put16(a, Centi(zenith_esn0_db));
    a.push_back(time_scale);
    return Function(payload::Function::kStartPass, std::move(a));
}

std::vector<std::uint8_t> Commander::StopPass()
{
    return Function(payload::Function::kStopPass, {});
}

std::vector<std::uint8_t> Commander::SetLoopback(std::uint8_t mode)
{
    return Function(payload::Function::kSetLoopback, {mode});
}

std::vector<std::uint8_t> Commander::RestartModem()
{
    return Function(payload::Function::kRestartModem, {});
}

std::vector<std::uint8_t> Commander::EnableHk(std::span<const std::uint8_t> sids, bool enable)
{
    std::vector<std::uint8_t> a{static_cast<std::uint8_t>(sids.size())};
    a.insert(a.end(), sids.begin(), sids.end());
    return Build(3, enable ? 5 : 6, std::move(a));
}

std::vector<std::uint8_t> Commander::OneShotHk(std::span<const std::uint8_t> sids)
{
    std::vector<std::uint8_t> a{static_cast<std::uint8_t>(sids.size())};
    a.insert(a.end(), sids.begin(), sids.end());
    return Build(3, 27, std::move(a));
}

std::vector<std::uint8_t> Commander::SetHkPeriod(std::uint8_t sid, std::uint32_t period_ms)
{
    std::vector<std::uint8_t> a{sid};
    Put16(a, static_cast<std::uint16_t>(period_ms >> 16U));
    Put16(a, static_cast<std::uint16_t>(period_ms & 0xFFFFU));
    return Build(3, 31, std::move(a));
}

} // namespace satlink::gs
