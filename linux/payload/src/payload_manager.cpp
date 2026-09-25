/**
 * @file payload_manager.cpp
 * @implements SRS-PLM-001
 * @implements SRS-PLM-002
 * @implements SRS-NET-001
 */
#include "satlink/payload/payload_manager.hpp"

#include <algorithm>
#include <cmath>

#include "satlink/modem/frame.h"

namespace satlink::payload {
namespace {

class ByteWriter
{
  public:
    ByteWriter &U8(std::uint8_t v)
    {
        bytes_.push_back(v);
        return *this;
    }
    ByteWriter &U16(std::uint16_t v)
    {
        bytes_.push_back(static_cast<std::uint8_t>(v >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        return *this;
    }
    ByteWriter &S16(std::int16_t v)
    {
        return U16(static_cast<std::uint16_t>(v));
    }
    ByteWriter &U32(std::uint32_t v)
    {
        U16(static_cast<std::uint16_t>(v >> 16U));
        return U16(static_cast<std::uint16_t>(v & 0xFFFFU));
    }
    std::vector<std::uint8_t> Take()
    {
        return std::move(bytes_);
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader
{
  public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}
    bool U8(std::uint8_t &v)
    {
        if (pos_ + 1 > data_.size())
        {
            return false;
        }
        v = data_[pos_++];
        return true;
    }
    bool U16(std::uint16_t &v)
    {
        if (pos_ + 2 > data_.size())
        {
            return false;
        }
        v = static_cast<std::uint16_t>((static_cast<unsigned>(data_[pos_]) << 8U) |
                                       data_[pos_ + 1]);
        pos_ += 2;
        return true;
    }
    bool S16(std::int16_t &v)
    {
        std::uint16_t u = 0;
        const bool ok = U16(u);
        v = static_cast<std::int16_t>(u);
        return ok;
    }
    bool U32(std::uint32_t &v)
    {
        std::uint16_t hi = 0;
        std::uint16_t lo = 0;
        const bool ok = U16(hi) && U16(lo);
        v = (static_cast<std::uint32_t>(hi) << 16U) | lo;
        return ok;
    }
    [[nodiscard]] bool AtEnd() const
    {
        return pos_ == data_.size();
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

std::int16_t ToCenti(double v)
{
    return static_cast<std::int16_t>(std::clamp(std::lround(v * 100.0), -32768L, 32767L));
}

constexpr std::uint64_t kPassUpdateMs = 250;
constexpr double kPassTailS = 10.0; ///< keep emulating a little after LOS

} // namespace

PayloadManager::PayloadManager(PayloadConfig config, amp::MessagePort &modem, GroundLink &ground,
                               Clock &clock, PacketTunnel *sat_tun, PacketTunnel *gnd_tun,
                               PlatformSource *platform)
    : config_(std::move(config)), client_(modem), ground_(ground), clock_(clock), sat_tun_(sat_tun),
      gnd_tun_(gnd_tun), platform_(platform), mux_(kSpacecraftId, config_.downlink_queue),
      demux_(kSpacecraftId,
             [this](std::uint8_t vc, std::span<const std::uint8_t> p) { OnPacket(vc, p); })
{
    hk_period_ms_.fill(config_.hk_period_ms);
    client_.on_rx_frame = [this](const amp::RxFrame &f) { OnRxFrame(f); };
    client_.on_status = [this](const satlink_msg_status_t &s) { OnStatus(s); };
    client_.on_log = [this](const amp::LogLine &l) { OnLog(l); };
}

void PayloadManager::Start()
{
    client_.SetTime(clock_.UnixMs());
    client_.Configure({true, true, config_.initial_modcod, config_.loopback});
    client_.SetAcm({config_.acm, 0, SATLINK_MODCOD_COUNT - 1, 100, 100});
    const std::uint64_t now = clock_.NowMs();
    for (auto &next : hk_next_ms_)
    {
        next = now + config_.hk_period_ms;
    }
    last_feed_ms_ = now;
}

void PayloadManager::Step()
{
    const std::uint64_t now = clock_.NowMs();
    while (auto datagram = ground_.ReceiveTc())
    {
        HandleTc(*datagram);
    }
    client_.Poll(std::chrono::milliseconds{0});
    ReadTunnels();
    UpdatePass(now);
    UpdateHousekeeping(now);
    FeedModem(now);
}

// ---- telecommands ----

void PayloadManager::Verification(const pus::Telecommand &tc, std::uint8_t subtype,
                                  std::optional<FailureCode> failure)
{
    const bool success = !failure.has_value();
    const std::uint8_t wanted = subtype <= 2 ? pus::kAckAcceptance : pus::kAckCompletion;
    if (success && (tc.ack_flags & wanted) == 0U)
    {
        return; // success reports only on request; failures always
    }
    ByteWriter w;
    w.U16(static_cast<std::uint16_t>(0x1800U | (tc.apid & 0x7FFU)))
        .U16(static_cast<std::uint16_t>(0xC000U | tc.sequence_count));
    if (failure)
    {
        w.U16(static_cast<std::uint16_t>(*failure));
    }
    EmitTm(1, subtype, w.Take());
}

void PayloadManager::HandleTc(std::span<const std::uint8_t> datagram)
{
    ++stats_.tc_received;
    pus::Telecommand tc;
    const pus::DecodeError err = pus::Decode(datagram, tc);
    if (err != pus::DecodeError::kNone)
    {
        ++stats_.tc_rejected;
        // Report against whatever header could be read (ECSS: acceptance failure).
        const auto h = pus::DecodePrimaryHeader(datagram);
        tc.apid = h ? h->apid : 0;
        tc.sequence_count = h ? h->sequence_count : 0;
        Verification(tc, 2, FailureCode::kCorruptPacket);
        return;
    }
    if (tc.apid != kApidPayload)
    {
        ++stats_.tc_rejected;
        Verification(tc, 2, FailureCode::kBadApid);
        return;
    }
    FailureCode failure = FailureCode::kBadData;
    const bool known = (tc.service == 17 && tc.subtype == 1) || tc.service == 3 ||
                       (tc.service == 8 && tc.subtype == 1);
    if (!known)
    {
        ++stats_.tc_rejected;
        Verification(tc, 2,
                     (tc.service == 17 || tc.service == 8) ? FailureCode::kUnknownSubtype
                                                           : FailureCode::kUnknownService);
        return;
    }
    Verification(tc, 1, std::nullopt);
    if (Execute(tc, failure))
    {
        Verification(tc, 7, std::nullopt);
    }
    else
    {
        ++stats_.tc_rejected;
        Verification(tc, 8, failure);
    }
}

bool PayloadManager::Execute(const pus::Telecommand &tc, FailureCode &failure)
{
    if (tc.service == 17)
    {
        EmitTm(17, 2, {});
        return true;
    }
    if (tc.service == 8)
    {
        return ExecuteFunction(tc.data, failure);
    }
    // ST[03]
    ByteReader r(tc.data);
    if (tc.subtype == 5 || tc.subtype == 6 || tc.subtype == 27)
    {
        std::uint8_t n = 0;
        if (!r.U8(n) || n == 0)
        {
            return false;
        }
        std::vector<std::uint8_t> sids(n);
        for (auto &sid : sids)
        {
            if (!r.U8(sid) || sid < 1 || sid > 3)
            {
                return false;
            }
        }
        if (!r.AtEnd())
        {
            return false;
        }
        for (const auto sid : sids)
        {
            if (tc.subtype == 27)
            {
                EmitTm(3, 25, HousekeepingData(static_cast<HkStructure>(sid)));
            }
            else
            {
                hk_enabled_[sid] = tc.subtype == 5;
            }
        }
        return true;
    }
    if (tc.subtype == 31)
    {
        std::uint8_t sid = 0;
        std::uint32_t period = 0;
        if (!r.U8(sid) || !r.U32(period) || !r.AtEnd() || sid < 1 || sid > 3 || period < 100 ||
            period > 60000)
        {
            return false;
        }
        hk_period_ms_[sid] = period;
        hk_next_ms_[sid] = clock_.NowMs() + period;
        return true;
    }
    failure = FailureCode::kUnknownSubtype;
    return false;
}

bool PayloadManager::ExecuteFunction(std::span<const std::uint8_t> data, FailureCode &failure)
{
    ByteReader r(data);
    std::uint8_t id = 0;
    if (!r.U8(id))
    {
        return false;
    }
    int rc = 0;
    switch (static_cast<Function>(id))
    {
    case Function::kSetModcod:
    {
        std::uint8_t modcod = 0;
        if (!r.U8(modcod) || !r.AtEnd() || modcod >= SATLINK_MODCOD_COUNT)
        {
            return false;
        }
        config_.acm = false;
        config_.initial_modcod = modcod;
        rc = client_.SetAcm({false, 0, SATLINK_MODCOD_COUNT - 1, 100, 100});
        if (rc == 0)
        {
            rc = client_.Configure({true, true, modcod, config_.loopback});
        }
        break;
    }
    case Function::kSetAcm:
    {
        std::uint8_t enable = 0;
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        std::int16_t margin = 0;
        std::int16_t hyst = 0;
        if (!r.U8(enable) || !r.U8(lo) || !r.U8(hi) || !r.S16(margin) || !r.S16(hyst) ||
            !r.AtEnd() || lo > hi || hi >= SATLINK_MODCOD_COUNT || margin < 0 || hyst < 0)
        {
            return false;
        }
        config_.acm = enable != 0;
        rc = client_.SetAcm({enable != 0, lo, hi, margin, hyst});
        break;
    }
    case Function::kSetChannel:
    {
        std::uint16_t noise = 0;
        std::uint16_t gain = 0;
        if (!r.U16(noise) || !r.U16(gain) || !r.AtEnd())
        {
            return false;
        }
        StopPass();
        rc = client_.SetChannel(noise, gain);
        break;
    }
    case Function::kStartPass:
    {
        std::uint16_t max_el = 0;
        std::int16_t zenith = 0;
        std::uint8_t scale = 0;
        if (!r.U16(max_el) || !r.S16(zenith) || !r.U8(scale) || !r.AtEnd() || max_el < 600 ||
            max_el > 9000 || scale == 0)
        {
            return false;
        }
        PassConfig pc;
        pc.max_elevation_deg = max_el / 100.0;
        pc.zenith_esn0_db = zenith / 100.0;
        StartPass(pc, scale);
        return true;
    }
    case Function::kStopPass:
        if (!r.AtEnd())
        {
            return false;
        }
        StopPass();
        return true;
    case Function::kSetLoopback:
    {
        std::uint8_t mode = 0;
        if (!r.U8(mode) || !r.AtEnd() || mode > SATLINK_LOOP_SOFTWARE)
        {
            return false;
        }
        config_.loopback = mode;
        rc = client_.Configure({true, true, config_.initial_modcod, mode});
        break;
    }
    case Function::kRestartModem:
        if (!r.AtEnd())
        {
            return false;
        }
        if (!config_.restart_modem)
        {
            failure = FailureCode::kModemError;
            return false;
        }
        rc = config_.restart_modem();
        if (rc == 0)
        {
            EmitEvent(Event::kModemRestarted, 2);
            Start(); // the new firmware instance needs its configuration again
        }
        break;
    default:
        failure = FailureCode::kUnknownFunction;
        return false;
    }
    if (rc != 0)
    {
        failure = FailureCode::kModemError;
        return false;
    }
    return true;
}

// ---- telemetry ----

void PayloadManager::EmitTm(std::uint8_t service, std::uint8_t subtype,
                            std::vector<std::uint8_t> data)
{
    pus::Telemetry tm;
    tm.apid = kApidPayload;
    tm.sequence_count = tm_seq_.Next();
    tm.service = service;
    tm.subtype = subtype;
    tm.message_counter = message_counter_++;
    tm.destination_id = kGroundId;
    tm.time = pus::CucTime::FromUnixMs(clock_.UnixMs());
    tm.data = std::move(data);
    auto packet = pus::Encode(tm);
    ++stats_.tm_generated;
    if (config_.tm_direct)
    {
        ground_.SendTm(packet);
    }
    mux_.Enqueue(kVcTelemetry, std::move(packet));
}

void PayloadManager::EmitEvent(Event id, std::uint8_t severity, std::vector<std::uint8_t> aux)
{
    ++stats_.events;
    ByteWriter w;
    w.U16(static_cast<std::uint16_t>(id));
    auto data = w.Take();
    data.insert(data.end(), aux.begin(), aux.end());
    EmitTm(5, severity, std::move(data));
}

std::vector<std::uint8_t> PayloadManager::HousekeepingData(HkStructure sid)
{
    ByteWriter w;
    w.U8(static_cast<std::uint8_t>(sid));
    switch (sid)
    {
    case HkStructure::kModem:
    {
        const satlink_msg_status_t s = client_.LastStatus().value_or(satlink_msg_status_t{});
        w.U8(s.locked).U8(s.tx_modcod).U8(s.acm_enabled).S16(s.esn0_cdb);
        w.U32(s.frames_ok).U32(s.frames_crc_error).U32(s.header_errors);
        w.U32(s.bit_errors).U32(s.bits_checked);
        w.U32(s.rx_latency_max_us).U32(s.rx_latency_avg_us).U16(s.cpu_load_permille);
        w.U8(s.tx_queue_depth).U32(s.uptime_ms);
        break;
    }
    case HkStructure::kLink:
    {
        const auto &d = demux_.Stats();
        w.U8(pass_.active ? 1 : 0).S16(ToCenti(pass_.sample.elevation_deg));
        w.U16(static_cast<std::uint16_t>(std::clamp(pass_.sample.range_km, 0.0, 65535.0)));
        w.S16(static_cast<std::int16_t>(
            std::clamp(pass_.sample.range_rate_km_s * 1000.0, -32768.0, 32767.0)));
        w.S16(ToCenti(pass_.sample.esn0_db));
        w.U32(stats_.frames_sent).U32(stats_.frames_received).U32(d.lost_frames);
        w.U32(d.packets).U32(d.resyncs).U32(stats_.ip_down).U32(stats_.ip_up);
        w.U32(mux_.Dropped());
        break;
    }
    case HkStructure::kPlatform:
    {
        const PlatformHk p = platform_ != nullptr ? platform_->Read() : PlatformHk{};
        w.U8(p.hkc_valid ? 1 : 0).S16(p.hkc_temp_centi_c).U16(p.hkc_vccint_mv);
        w.U16(p.hkc_vccaux_mv).U16(p.hkc_vbram_mv).U32(p.hkc_uptime_s).U8(p.hkc_error_flags);
        w.U8(p.rtos_state).U32(p.rtos_restarts);
        break;
    }
    }
    return w.Take();
}

void PayloadManager::UpdateHousekeeping(std::uint64_t now)
{
    for (std::uint8_t sid = 1; sid <= 3; ++sid)
    {
        if (!hk_enabled_[sid] || now < hk_next_ms_[sid])
        {
            continue;
        }
        hk_next_ms_[sid] = now + hk_period_ms_[sid];
        EmitTm(3, 25, HousekeepingData(static_cast<HkStructure>(sid)));
    }
}

// ---- modem link ----

void PayloadManager::OnRxFrame(const amp::RxFrame &frame)
{
    if (!frame.crc_ok)
    {
        ++stats_.frames_crc_error;
        return;
    }
    ++stats_.frames_received;
    demux_.Push(frame.data);
}

void PayloadManager::OnPacket(std::uint8_t vc, std::span<const std::uint8_t> packet)
{
    const auto h = pus::DecodePrimaryHeader(packet);
    if (!h)
    {
        return;
    }
    if (vc == kVcTelemetry && h->apid == kApidPayload)
    {
        ++stats_.tm_delivered;
        if (!config_.tm_direct)
        {
            ground_.SendTm(packet);
        }
        return;
    }
    if (vc == kVcIp && packet.size() > pus::kPrimaryHeaderSize)
    {
        const auto datagram = packet.subspan(pus::kPrimaryHeaderSize);
        if (h->apid == kApidIpDown && gnd_tun_ != nullptr)
        {
            ++stats_.ip_down;
            gnd_tun_->Write(datagram);
        }
        else if (h->apid == kApidIpUp && sat_tun_ != nullptr)
        {
            ++stats_.ip_up;
            sat_tun_->Write(datagram);
        }
    }
}

void PayloadManager::ReadTunnels()
{
    // Bounded per step so one busy tunnel cannot starve the rest of the loop.
    for (int i = 0; i < 16 && sat_tun_ != nullptr; ++i)
    {
        auto ip = sat_tun_->Read();
        if (!ip)
        {
            break;
        }
        mux_.Enqueue(kVcIp, pus::EncodeRaw(pus::PacketType::kTelemetry, kApidIpDown,
                                           ip_down_seq_.Next(), *ip));
    }
    for (int i = 0; i < 16 && gnd_tun_ != nullptr; ++i)
    {
        auto ip = gnd_tun_->Read();
        if (!ip)
        {
            break;
        }
        mux_.Enqueue(
            kVcIp, pus::EncodeRaw(pus::PacketType::kTelemetry, kApidIpUp, ip_up_seq_.Next(), *ip));
    }
}

void PayloadManager::OnStatus(const satlink_msg_status_t &status)
{
    queue_estimate_ = status.tx_queue_depth;
    const bool locked = status.locked != 0;
    if (locked != was_locked_)
    {
        EmitEvent(locked ? Event::kLinkLocked : Event::kLinkLost, locked ? 1 : 2);
        was_locked_ = locked;
    }
}

void PayloadManager::OnLog(const amp::LogLine &line)
{
    // "ACM 1->2 up at 12.3 dB"
    if (line.text.rfind("ACM ", 0) == 0 && line.text.size() >= 8)
    {
        const auto from = static_cast<std::uint8_t>(line.text[4] - '0');
        const auto to = static_cast<std::uint8_t>(line.text[7] - '0');
        EmitEvent(Event::kModcodChanged, 1, {from, to});
        return;
    }
    if (line.level >= 2)
    {
        std::vector<std::uint8_t> text(line.text.begin(), line.text.end());
        EmitEvent(Event::kFirmwareLog, line.level >= 3 ? 3 : 2, std::move(text));
    }
}

void PayloadManager::FeedModem(std::uint64_t now)
{
    // Store and forward: without lock nothing would arrive, so keep the packets queued; the
    // modem sends idle frames meanwhile, which is what lets the receiver lock again.
    // The pass schedule is known on board as well: below the horizon there is no contact even
    // before the (once per second) lock status notices.
    const auto status = client_.LastStatus();
    if (!status || status->locked == 0 || (pass_.active && !pass_.visible))
    {
        last_feed_ms_ = now;
        return;
    }
    // The modem drains one frame per frame time; between STATUS messages, model that.
    const satlink_msg_status_t &s = *status;
    const double frame_ms = static_cast<double>(satlink_frame_symbols(s.tx_modcod)) / 6.0;
    if (frame_ms > 0.0)
    {
        queue_estimate_ =
            std::max(0.0, queue_estimate_ - static_cast<double>(now - last_feed_ms_) / frame_ms);
    }
    last_feed_ms_ = now;
    while (mux_.HasData() && queue_estimate_ < static_cast<double>(config_.max_frames_queued))
    {
        const pus::Frame frame = mux_.NextFrame();
        if (client_.SendFrame(frame) != 0)
        {
            break;
        }
        ++stats_.frames_sent;
        queue_estimate_ += 1.0;
    }
}

// ---- pass emulation ----

void PayloadManager::StartPass(const PassConfig &config, double time_scale)
{
    leo_ = std::make_unique<LeoPass>(config);
    pass_ = PassState{};
    pass_.active = leo_->DurationS() > 0.0;
    pass_.time_scale = time_scale;
    pass_.start_ms = clock_.NowMs();
    pass_last_update_ms_ = 0;
    UpdatePass(pass_.start_ms);
}

void PayloadManager::StopPass()
{
    if (pass_.active)
    {
        pass_.active = false;
        client_.SetChannel(0, 0x7FFF);
    }
}

void PayloadManager::UpdatePass(std::uint64_t now)
{
    if (!pass_.active || (pass_last_update_ms_ != 0 && now - pass_last_update_ms_ < kPassUpdateMs))
    {
        return;
    }
    pass_last_update_ms_ = now;
    const double t = leo_->AosS() - 5.0 +
                     (static_cast<double>(now - pass_.start_ms) / 1000.0) * pass_.time_scale;
    pass_.sample = leo_->At(t);
    if (pass_.sample.visible != pass_.visible)
    {
        pass_.visible = pass_.sample.visible;
        EmitEvent(pass_.visible ? Event::kAos : Event::kLos, 1);
    }
    const ChannelSetting ch =
        ChannelFor(pass_.sample.esn0_db, pass_.sample.visible, config_.noise_scale);
    client_.SetChannel(ch.noise_level, ch.gain_q15);
    if (t > -leo_->AosS() + kPassTailS)
    {
        StopPass();
    }
}

} // namespace satlink::payload
