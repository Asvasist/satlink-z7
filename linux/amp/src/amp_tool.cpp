/**
 * @file amp_tool.cpp
 * @brief satlink-amp: start, stop and inspect the Core 1 modem firmware from the shell.
 *
 *   satlink-amp status                      driver and firmware state, ring usage, counters
 *   satlink-amp start | stop
 *   satlink-amp ping
 *   satlink-amp config <modcod> [loop=analog|digital|software] [tx=on|off] [rx=on|off]
 *   satlink-amp channel <noise_level> [gain_q15]
 *   satlink-amp monitor [seconds]            print STATUS, LOG and RX_FRAME messages
 *
 * @implements SRS-AMP-003
 */
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <satlink/amp.h>
#include <string>
#include <system_error>
#include <vector>

#include "satlink/amp_client/amp_device.hpp"
#include "satlink/amp_client/modem_client.hpp"

namespace {

const char *StateName(std::uint32_t state)
{
    static const char *const k_names[] = {"offline", "booting", "running", "fault", "crashed"};
    return state < 5 ? k_names[state] : "?";
}

void PrintStatus(const satlink_msg_status_t &s)
{
    std::printf("t=%us lock=%u modcod=%u EsN0=%.2f dB frames ok=%u crc=%u hdr=%u "
                "BER=%u/%u txq=%u load=%.1f%% rx_latency max/avg=%u/%u us\n",
                s.uptime_ms / 1000U, s.locked, s.tx_modcod, s.esn0_cdb / 100.0, s.frames_ok,
                s.frames_crc_error, s.header_errors, s.bit_errors, s.bits_checked, s.tx_queue_depth,
                s.cpu_load_permille / 10.0, s.rx_latency_max_us, s.rx_latency_avg_us);
}

int Usage()
{
    std::cerr << "usage: satlink-amp status | start | stop | ping | config <modcod> [loop=analog|"
                 "digital|software] [tx=on|off] [rx=on|off] | channel <noise> [gain_q15] | "
                 "monitor [seconds]\n";
    return 2;
}

int Run(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        return Usage();
    }
    satlink::amp::AmpDevice dev;
    satlink::amp::ModemClient client(dev);
    const std::string &cmd = args[0];

    if (cmd == "status")
    {
        satlink_amp_status st{};
        if (const int rc = dev.GetStatus(st); rc != 0)
        {
            throw std::system_error(-rc, std::generic_category(), "GET_STATUS");
        }
        std::printf("state %s, firmware %u.%u.%u, boot %u, restarts %u, heartbeat %u\n"
                    "fault code %u at 0x%08x\nrings: to rtos %u B, to linux %u B; msgs tx %u "
                    "rx %u, doorbells %u\n",
                    StateName(st.state), st.fw_version >> 16, (st.fw_version >> 8) & 0xFF,
                    st.fw_version & 0xFF, st.boot_count, st.restarts, st.heartbeat, st.fault_code,
                    st.fault_addr, st.to_rtos_used, st.to_linux_used, st.tx_msgs, st.rx_msgs,
                    st.irqs);
        return 0;
    }
    if (cmd == "start" || cmd == "stop")
    {
        const int rc = cmd == "start" ? dev.Start() : dev.Stop();
        if (rc != 0)
        {
            throw std::system_error(-rc, std::generic_category(), cmd);
        }
        return 0;
    }
    if (cmd == "ping")
    {
        const auto uptime = client.PingAndWait(0x5A71u, std::chrono::milliseconds{1000});
        if (!uptime)
        {
            std::cerr << "satlink-amp: no PONG\n";
            return 3;
        }
        std::printf("PONG: firmware up %u ms\n", *uptime);
        return 0;
    }
    if (cmd == "config" && args.size() >= 2)
    {
        satlink_msg_modem_config_t cfg{true, true, 0, SATLINK_LOOP_ANALOG};
        cfg.modcod = static_cast<std::uint8_t>(std::stoul(args[1]));
        for (std::size_t i = 2; i < args.size(); ++i)
        {
            const std::string &a = args[i];
            if (a == "loop=analog")
                cfg.loopback = SATLINK_LOOP_ANALOG;
            else if (a == "loop=digital")
                cfg.loopback = SATLINK_LOOP_DIGITAL;
            else if (a == "loop=software")
                cfg.loopback = SATLINK_LOOP_SOFTWARE;
            else if (a == "tx=off" || a == "tx=on")
                cfg.tx_enable = a == "tx=on";
            else if (a == "rx=off" || a == "rx=on")
                cfg.rx_enable = a == "rx=on";
            else
                return Usage();
        }
        return client.Configure(cfg) == 0 ? 0 : 1;
    }
    if (cmd == "channel" && args.size() >= 2)
    {
        const auto noise = static_cast<std::uint16_t>(std::stoul(args[1]));
        const auto gain =
            static_cast<std::uint16_t>(args.size() > 2 ? std::stoul(args[2]) : 0x7FFFUL);
        return client.SetChannel(noise, gain) == 0 ? 0 : 1;
    }
    if (cmd == "monitor")
    {
        const unsigned seconds = args.size() > 1 ? static_cast<unsigned>(std::stoul(args[1])) : 10;
        client.on_status = PrintStatus;
        client.on_log = [](const satlink::amp::LogLine &l) {
            std::printf("log[%u]: %s\n", l.level, l.text.c_str());
        };
        client.on_rx_frame = [](const satlink::amp::RxFrame &f) {
            std::printf("rx frame seq=%u modcod=%u crc=%s EsN0=%.2f dB\n", f.seq, f.modcod,
                        f.crc_ok ? "ok" : "BAD", static_cast<double>(f.esn0_db));
        };
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
        while (std::chrono::steady_clock::now() < end)
        {
            if (client.Poll(std::chrono::milliseconds{200}) < 0)
            {
                std::cerr << "satlink-amp: firmware not running\n";
                return 1;
            }
        }
        return 0;
    }
    return Usage();
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        return Run(std::vector<std::string>(argv + 1, argv + argc));
    }
    catch (const std::exception &e)
    {
        std::cerr << "satlink-amp: " << e.what() << "\n";
        return 1;
    }
}
