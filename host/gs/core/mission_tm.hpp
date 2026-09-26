/**
 * @file mission_tm.hpp
 * @brief Ground side of the mission PUS definitions: telecommand builders and decoders for the
 *        payload's telemetry reports (docs/icd section 5). No Qt: shared by the ground station,
 *        the unit tests and, through the same layouts, the Python and Rust clients.
 *
 * @implements SRS-GS-001
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "satlink/payload/mission.hpp"
#include "satlink/pus/space_packet.hpp"

namespace satlink::gs {

/// ST[03] structure 1.
struct ModemHk
{
    bool locked = false;
    std::uint8_t modcod = 0;
    bool acm = false;
    double esn0_db = 0.0;
    std::uint32_t frames_ok = 0;
    std::uint32_t frames_crc_error = 0;
    std::uint32_t header_errors = 0;
    std::uint32_t bit_errors = 0;
    std::uint32_t bits_checked = 0;
    std::uint32_t latency_max_us = 0;
    std::uint32_t latency_avg_us = 0;
    double cpu_load_pct = 0.0;
    std::uint8_t tx_queue = 0;
    std::uint32_t uptime_ms = 0;
};

/// ST[03] structure 2.
struct LinkHk
{
    bool pass_active = false;
    double elevation_deg = 0.0;
    double range_km = 0.0;
    double range_rate_km_s = 0.0;
    double pass_esn0_db = 0.0;
    std::uint32_t frames_sent = 0;
    std::uint32_t frames_received = 0;
    std::uint32_t lost_frames = 0;
    std::uint32_t packets = 0;
    std::uint32_t resyncs = 0;
    std::uint32_t ip_down = 0;
    std::uint32_t ip_up = 0;
    std::uint32_t dropped = 0;
};

/// ST[03] structure 3.
struct PlatformHk
{
    bool hkc_valid = false;
    double temperature_c = 0.0;
    std::uint16_t vccint_mv = 0;
    std::uint16_t vccaux_mv = 0;
    std::uint16_t vbram_mv = 0;
    std::uint32_t hkc_uptime_s = 0;
    std::uint8_t hkc_error_flags = 0;
    std::uint8_t rtos_state = 0;
    std::uint32_t rtos_restarts = 0;
};

/// ST[03] structure 4: received symbols after gain and carrier correction.
struct ConstellationHk
{
    std::uint8_t modcod = 0;
    std::vector<std::pair<double, double>> points; ///< (I, Q), unit symbol amplitude = 1
};

/// ST[05] event report.
struct EventReport
{
    std::uint8_t severity = 1; ///< subtype: 1 info ... 4 high
    payload::Event id = payload::Event::kLinkLocked;
    std::vector<std::uint8_t> aux;

    /// One line for a log ("MODCOD 1 -> 2", "firmware: <text>").
    [[nodiscard]] std::string Describe() const;
};

/// ST[01] verification report.
struct VerificationReport
{
    std::uint8_t subtype = 1; ///< 1 acc ok, 2 acc fail, 7 done, 8 failed
    std::uint16_t apid = 0;
    std::uint16_t sequence_count = 0;
    std::optional<payload::FailureCode> failure;

    [[nodiscard]] bool Success() const
    {
        return subtype == 1 || subtype == 3 || subtype == 5 || subtype == 7;
    }
};

/// ST[17] are-you-alive answer.
struct PingReport
{};

/// Anything else (or a report whose length does not match its layout).
struct UnknownReport
{};

using Report = std::variant<UnknownReport, ModemHk, LinkHk, PlatformHk, ConstellationHk,
                            EventReport, VerificationReport, PingReport>;

/// Decodes the application data of a payload TM packet.
Report Interpret(const pus::Telemetry &tm);

std::optional<ModemHk> DecodeModemHk(std::span<const std::uint8_t> data);
std::optional<LinkHk> DecodeLinkHk(std::span<const std::uint8_t> data);
std::optional<PlatformHk> DecodePlatformHk(std::span<const std::uint8_t> data);
std::optional<ConstellationHk> DecodeConstellationHk(std::span<const std::uint8_t> data);

std::string EventName(payload::Event id);
std::string FailureName(payload::FailureCode code);
std::string ModcodName(std::uint8_t modcod);

/// Noise level of the channel emulator for an Es/N0 (the payload's noise scale is 4096).
std::uint16_t NoiseLevelFor(double esn0_db, double noise_scale = 4096.0);

/// Builds telecommands with a running sequence count.
class Commander
{
  public:
    explicit Commander(std::uint16_t apid = payload::kApidPayload) : apid_(apid) {}

    std::vector<std::uint8_t> Build(std::uint8_t service, std::uint8_t subtype,
                                    std::vector<std::uint8_t> data = {});

    std::vector<std::uint8_t> Ping()
    {
        return Build(17, 1);
    }
    std::vector<std::uint8_t> SetModcod(std::uint8_t modcod);
    std::vector<std::uint8_t> SetAcm(bool enabled, std::uint8_t min_modcod, std::uint8_t max_modcod,
                                     double margin_db, double hysteresis_db);
    std::vector<std::uint8_t> SetChannel(std::uint16_t noise_level, std::uint16_t gain_q15);
    std::vector<std::uint8_t> StartPass(double max_elevation_deg, double zenith_esn0_db,
                                        std::uint8_t time_scale);
    std::vector<std::uint8_t> StopPass();
    std::vector<std::uint8_t> SetLoopback(std::uint8_t mode);
    std::vector<std::uint8_t> RestartModem();
    std::vector<std::uint8_t> EnableHk(std::span<const std::uint8_t> sids, bool enable);
    std::vector<std::uint8_t> OneShotHk(std::span<const std::uint8_t> sids);
    std::vector<std::uint8_t> SetHkPeriod(std::uint8_t sid, std::uint32_t period_ms);

    /// Sequence count the next Build() will use.
    [[nodiscard]] std::uint16_t NextSequence() const
    {
        return seq_.Peek();
    }

  private:
    std::vector<std::uint8_t> Function(payload::Function f, std::vector<std::uint8_t> args);

    std::uint16_t apid_;
    pus::SequenceCounter seq_;
};

} // namespace satlink::gs
