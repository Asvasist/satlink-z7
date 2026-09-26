/**
 * @file scpi_instrument.cpp
 * @implements SRS-SCPI-002
 */
#include "satlink/payload/scpi_instrument.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <limits>
#include <utility>

#include "satlink/modem/modcod.h"

namespace satlink::payload {

using scpi::Call;
using scpi::Error;

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr std::uint8_t kTopModcod = SATLINK_MODCOD_COUNT - 1U;
constexpr long kTop = kTopModcod;

double Ratio(std::uint32_t num, std::uint32_t den)
{
    return den == 0U ? kNan : static_cast<double>(num) / static_cast<double>(den);
}

std::int16_t Centi(double db)
{
    return static_cast<std::int16_t>(std::lround(db * 100.0));
}

} // namespace

ScpiInstrument::ScpiInstrument(PayloadManager &manager, std::string serial, std::string version)
    : manager_(manager), defaults_(manager.Config()),
      idn_("SatLink-Z7,Payload Modem," + std::move(serial) + "," + std::move(version))
{
    parser_.Add("*IDN?", [this](Call &c) { c.Reply(std::string_view(idn_)); });
    parser_.Add("*RST", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            Reset();
        }
    });
    // Self-test: the modem firmware answers with status reports.
    parser_.Add("*TST?", [this](Call &c) { c.Reply(manager_.ModemStatus() ? 0L : 1L); });
    parser_.Add("SYSTem:COUNters?", [this](Call &c) {
        const PayloadStats &s = manager_.Stats();
        for (const std::uint32_t v :
             {s.tc_received, s.tc_rejected, s.tm_generated, s.tm_delivered, s.frames_sent,
              s.frames_received, s.frames_crc_error, s.ip_down, s.ip_up, s.events})
        {
            c.Reply(static_cast<long>(v));
        }
    });
    AddModem();
    AddChannel();
    AddMeasure();
    AddPass();
}

void ScpiInstrument::Check(Call &call, int rc)
{
    if (rc == -ENOTSUP)
    {
        call.Fail(Error::kHardwareMissing);
    }
    else if (rc != 0)
    {
        call.Fail(Error::kExecution, "modem port error " + std::to_string(rc));
    }
}

satlink_msg_status_t ScpiInstrument::Status() const
{
    return manager_.ModemStatus().value_or(satlink_msg_status_t{});
}

satlink_msg_acm_config_t ScpiInstrument::Acm() const
{
    return manager_.AcmConfig().value_or(satlink_msg_acm_config_t{false, 0, kTopModcod, 100, 100});
}

void ScpiInstrument::Reset()
{
    manager_.StopPass();
    (void)manager_.SetChannel(0, 0x7FFF);
    (void)manager_.SetLoopback(defaults_.loopback);
    (void)manager_.SetModcod(defaults_.initial_modcod);
    if (defaults_.acm)
    {
        (void)manager_.SetAcm({true, 0, kTopModcod, 100, 100});
    }
    baseline_ = Status();
}

void ScpiInstrument::AddModem()
{
    parser_.Add("MODem:MODCod", [this](Call &c) {
        if (const auto m = c.Integer(0, 0, kTop); m && c.Expect(1, 1))
        {
            Check(c, manager_.SetModcod(static_cast<std::uint8_t>(*m)));
        }
    });
    parser_.Add("MODem:MODCod?",
                [this](Call &c) { c.Reply(static_cast<long>(Status().tx_modcod)); });
    parser_.Add("MODem:ACM[:STATe]", [this](Call &c) {
        if (const auto on = c.Boolean(0); on && c.Expect(1, 1))
        {
            satlink_msg_acm_config_t acm = Acm();
            acm.enabled = *on;
            Check(c, manager_.SetAcm(acm));
        }
    });
    parser_.Add("MODem:ACM[:STATe]?", [this](Call &c) { c.Reply(Acm().enabled); });
    parser_.Add("MODem:ACM:LIMits", [this](Call &c) {
        const auto lo = c.Integer(0, 0, kTop, 0);
        const auto hi = c.Integer(1, 0, kTop, kTop);
        if (!lo || !hi || !c.Expect(2, 2))
        {
            return;
        }
        if (*lo > *hi)
        {
            c.Fail(Error::kSettingsConflict, "minimum above maximum");
            return;
        }
        satlink_msg_acm_config_t acm = Acm();
        acm.min_modcod = static_cast<std::uint8_t>(*lo);
        acm.max_modcod = static_cast<std::uint8_t>(*hi);
        Check(c, manager_.SetAcm(acm));
    });
    parser_.Add("MODem:ACM:LIMits?", [this](Call &c) {
        c.Reply(static_cast<long>(Acm().min_modcod));
        c.Reply(static_cast<long>(Acm().max_modcod));
    });
    parser_.Add("MODem:ACM:MARGin", [this](Call &c) {
        if (const auto db = c.Number(0, 0.0, 10.0, 1.0, {"DB"}); db && c.Expect(1, 1))
        {
            satlink_msg_acm_config_t acm = Acm();
            acm.margin_cdb = Centi(*db);
            Check(c, manager_.SetAcm(acm));
        }
    });
    parser_.Add("MODem:ACM:MARGin?", [this](Call &c) { c.Reply(Acm().margin_cdb / 100.0); });
    parser_.Add("MODem:ACM:HYSTeresis", [this](Call &c) {
        if (const auto db = c.Number(0, 0.0, 10.0, 1.0, {"DB"}); db && c.Expect(1, 1))
        {
            satlink_msg_acm_config_t acm = Acm();
            acm.hysteresis_cdb = Centi(*db);
            Check(c, manager_.SetAcm(acm));
        }
    });
    parser_.Add("MODem:ACM:HYSTeresis?",
                [this](Call &c) { c.Reply(Acm().hysteresis_cdb / 100.0); });
    parser_.Add("MODem:LOOPback", [this](Call &c) {
        if (const auto m = c.Choice(0, {"ANALog", "DIGital", "SOFTware"}); m && c.Expect(1, 1))
        {
            Check(c, manager_.SetLoopback(static_cast<std::uint8_t>(*m)));
        }
    });
    parser_.Add("MODem:LOOPback?", [this](Call &c) {
        static constexpr std::array<std::string_view, 3> kNames = {"ANAL", "DIG", "SOFT"};
        c.Reply(kNames[manager_.Config().loopback % 3U]);
    });
    parser_.Add("MODem:RESTart", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            Check(c, manager_.RestartModem());
        }
    });
}

void ScpiInstrument::AddChannel()
{
    parser_.Add("CHANnel:ESN0", [this](Call &c) {
        if (const auto db = c.Number(0, -10.0, 60.0, std::nullopt, {"DB"}); db && c.Expect(1, 1))
        {
            Check(c, manager_.SetEsN0(*db));
        }
    });
    // Nominal Es/N0 of the static channel setting (NaN without noise).
    parser_.Add("CHANnel:ESN0?", [this](Call &c) {
        const satlink_msg_channel_t &ch = manager_.Channel();
        if (ch.noise_level == 0U || ch.gain_q15 == 0U)
        {
            c.Reply(ch.gain_q15 == 0U ? -std::numeric_limits<double>::infinity() : kNan);
            return;
        }
        const double sigma = ch.noise_level / manager_.Config().noise_scale;
        const double gain = ch.gain_q15 / 32767.0;
        c.Reply(std::round(200.0 * std::log10(gain / sigma)) / 10.0);
    });
    parser_.Add("CHANnel:NOISe", [this](Call &c) {
        if (const auto v = c.Integer(0, 0, 65535, 0); v && c.Expect(1, 1))
        {
            Check(c,
                  manager_.SetChannel(static_cast<std::uint16_t>(*v), manager_.Channel().gain_q15));
        }
    });
    parser_.Add("CHANnel:NOISe?",
                [this](Call &c) { c.Reply(static_cast<long>(manager_.Channel().noise_level)); });
    parser_.Add("CHANnel:GAIN", [this](Call &c) {
        if (const auto v = c.Integer(0, 0, 32767, 32767); v && c.Expect(1, 1))
        {
            Check(c, manager_.SetChannel(manager_.Channel().noise_level,
                                         static_cast<std::uint16_t>(*v)));
        }
    });
    parser_.Add("CHANnel:GAIN?",
                [this](Call &c) { c.Reply(static_cast<long>(manager_.Channel().gain_q15)); });
    parser_.Add("CHANnel:CLEar", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            Check(c, manager_.SetChannel(0, 0x7FFF));
        }
    });
}

void ScpiInstrument::AddMeasure()
{
    parser_.Add("MEASure:ESN0?", [this](Call &c) {
        if (!manager_.ModemStatus())
        {
            c.Reply(kNan);
            return;
        }
        c.Reply(Status().esn0_cdb / 100.0);
    });
    parser_.Add("MEASure:LOCK?", [this](Call &c) { c.Reply(Status().locked != 0U); });
    parser_.Add("MEASure:MODCod?",
                [this](Call &c) { c.Reply(static_cast<long>(Status().tx_modcod)); });
    parser_.Add("MEASure:LOAD?", [this](Call &c) { c.Reply(Status().cpu_load_permille / 10.0); });
    parser_.Add("MEASure:LATency?", [this](Call &c) {
        c.Reply(static_cast<long>(Status().rx_latency_max_us));
        c.Reply(static_cast<long>(Status().rx_latency_avg_us));
    });
    parser_.Add("MEASure:COUNters?", [this](Call &c) {
        const satlink_msg_status_t s = Status();
        for (const std::uint32_t v :
             {s.frames_ok, s.frames_crc_error, s.bit_errors, s.bits_checked})
        {
            c.Reply(static_cast<long>(v));
        }
    });
    parser_.Add("MEASure:RESet", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            baseline_ = Status();
        }
    });
    // Counters are cumulative in the firmware; a restart of Core 1 resets them, which also
    // restarts the window.
    auto window = [this]() {
        const satlink_msg_status_t s = Status();
        if (s.bits_checked < baseline_.bits_checked || s.frames_ok < baseline_.frames_ok ||
            s.frames_crc_error < baseline_.frames_crc_error)
        {
            baseline_ = satlink_msg_status_t{};
        }
        satlink_msg_status_t d = s;
        d.frames_ok -= baseline_.frames_ok;
        d.frames_crc_error -= baseline_.frames_crc_error;
        d.bit_errors -= baseline_.bit_errors;
        d.bits_checked -= baseline_.bits_checked;
        return d;
    };
    parser_.Add("MEASure:BER?", [window](Call &c) {
        const satlink_msg_status_t d = window();
        c.Reply(Ratio(d.bit_errors, d.bits_checked));
    });
    parser_.Add("MEASure:FER?", [window](Call &c) {
        const satlink_msg_status_t d = window();
        c.Reply(Ratio(d.frames_crc_error, d.frames_ok + d.frames_crc_error));
    });
}

void ScpiInstrument::AddPass()
{
    parser_.Add("PASS:STARt", [this](Call &c) {
        if (!c.Expect(0, 3))
        {
            return;
        }
        PassConfig pc;
        double scale = 1.0;
        if (c.Count() > 0)
        {
            const auto el = c.Number(0, 6.0, 90.0, 60.0, {"DEG"});
            if (!el)
            {
                return;
            }
            pc.max_elevation_deg = *el;
        }
        if (c.Count() > 1)
        {
            const auto z = c.Number(1, -10.0, 40.0, 20.0, {"DB"});
            if (!z)
            {
                return;
            }
            pc.zenith_esn0_db = *z;
        }
        if (c.Count() > 2)
        {
            const auto s = c.Number(2, 1.0, 255.0, 1.0);
            if (!s)
            {
                return;
            }
            scale = *s;
        }
        manager_.StartPass(pc, scale);
    });
    parser_.Add("PASS:STOP", [this](Call &c) {
        if (c.Expect(0, 0))
        {
            manager_.StopPass();
        }
    });
    parser_.Add("PASS:STATe?", [this](Call &c) {
        const PassState &p = manager_.Pass();
        c.Reply(p.active);
        c.Reply(std::round(p.sample.elevation_deg * 100.0) / 100.0);
        c.Reply(std::round(p.sample.range_km * 10.0) / 10.0);
        c.Reply(std::round(p.sample.esn0_db * 100.0) / 100.0);
        c.Reply(p.visible);
    });
}

} // namespace satlink::payload
