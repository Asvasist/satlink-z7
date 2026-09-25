/**
 * @file scpi_instrument.hpp
 * @brief The payload as a SCPI instrument: the command tree of satlink-payloadd's TCP port
 *        5025, so the testbed can be scripted like bench equipment (pyVISA, LabVIEW, a
 *        terminal).
 *
 * | Command                                   | Meaning                                       |
 * |-------------------------------------------|-----------------------------------------------|
 * | *IDN?  *RST  *TST?                        | identity, defaults, self-test (0 = pass)      |
 * | MODem:MODCod <0..4> / ?                   | fixed MODCOD (ACM off) / MODCOD in use        |
 * | MODem:ACM[:STATe] ON|OFF / ?              | adaptive coding and modulation                |
 * | MODem:ACM:LIMits <min>,<max> / ?          | MODCOD range for ACM                          |
 * | MODem:ACM:MARGin <dB> / ?                 | margin above the MODCOD thresholds            |
 * | MODem:ACM:HYSTeresis <dB> / ?             | switch-up hysteresis                          |
 * | MODem:LOOPback ANALog|DIGital|SOFTware / ?| loopback point                                |
 * | MODem:RESTart                             | restart the Core 1 firmware                   |
 * | CHANnel:ESN0 <dB> / ?                     | channel emulator set for an Es/N0             |
 * | CHANnel:NOISe <0..65535> / ?              | raw noise level                               |
 * | CHANnel:GAIN <0..32767> / ?               | raw gain, Q15                                 |
 * | CHANnel:CLEar                             | no noise, 0 dB gain                           |
 * | MEASure:ESN0? LOCK? MODCod? LOAD?         | receiver Es/N0 (dB), lock, MODCOD, CPU %      |
 * | MEASure:BER? FER?                         | since the last MEASure:RESet                  |
 * | MEASure:COUNters?                         | frames ok, CRC errors, bit errors, bits       |
 * | MEASure:LATency?                          | receive latency max, average (us)             |
 * | MEASure:RESet                             | restart the BER / FER window                  |
 * | PASS:STARt [<max el>[,<zenith dB>[,<x>]]] | emulate a LEO pass                            |
 * | PASS:STOP                                 | end it, channel back to the static setting    |
 * | PASS:STATe?                               | active, elevation, range, Es/N0, visible      |
 * | SYSTem:COUNters?                          | TC rx/rejected, TM generated/delivered, ...   |
 *
 * @implements SRS-SCPI-002
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "satlink/payload/payload_manager.hpp"
#include "satlink/scpi/parser.hpp"

namespace satlink::payload {

class ScpiInstrument
{
  public:
    ScpiInstrument(PayloadManager &manager, std::string serial, std::string version);

    /// Executes one line; returns the response line without terminator (empty: none).
    std::string Execute(std::string_view line)
    {
        return parser_.Execute(line);
    }

    [[nodiscard]] scpi::Parser &Scpi()
    {
        return parser_;
    }

  private:
    void AddModem();
    void AddChannel();
    void AddMeasure();
    void AddPass();
    void Reset();
    static void Check(scpi::Call &call, int rc);
    [[nodiscard]] satlink_msg_status_t Status() const;
    [[nodiscard]] satlink_msg_acm_config_t Acm() const;

    PayloadManager &manager_;
    PayloadConfig defaults_;
    std::string idn_;
    scpi::Parser parser_;
    satlink_msg_status_t baseline_{}; ///< counters at the last MEASure:RESet
};

} // namespace satlink::payload
