/**
 * @file main.cpp
 * @brief satlink-payloadd: the payload manager daemon.
 *
 *   satlink-payloadd [options]
 *     --sim                 run the modem firmware logic in-process (no board needed)
 *     --amp PATH            AMP device (default /dev/satlink-amp)
 *     --tc-port N           UDP port for telecommands (default 10025)
 *     --gs HOST[:PORT]      ground station for telemetry (default: whoever sends TCs, port 10026)
 *     --tm-direct           also send every TM packet straight to the ground station
 *     --tun                 create the IP endpoints satlink-sat and satlink-gnd
 *     --can IFACE           read housekeeping controller telemetry from this CAN interface
 *     --loopback MODE       software | digital | analog (default software)
 *     --modcod N            initial MODCOD (default 1)
 *     --no-acm              fixed MODCOD
 *     --scpi-port N         SCPI instrument port (TCP, default 5025; 0 = off)
 *     --serial S            serial number reported by *IDN? (default 0)
 *     --verbose             print a status line every 5 s
 *
 * Under systemd (Type=notify) the daemon reports READY=1 once configured and pings the service
 * watchdog from its main loop: a hung loop gets the service restarted.
 *
 * @implements SRS-PLM-003
 * @implements SRS-SCPI-002
 * @implements SRS-FDIR-002
 */
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <poll.h>
#include <satlink/amp.h>
#include <string>
#include <vector>

#include "satlink/amp_client/simulated_core1.hpp"
#include "satlink/common/version.h"
#include "satlink/hal/socket_can_bus.hpp"
#include "satlink/payload/payload_manager.hpp"
#include "satlink/payload/posix_io.hpp"
#include "satlink/payload/scpi_instrument.hpp"

#if defined(SATLINK_HAVE_AMP_DEVICE)
#include "satlink/amp_client/amp_device.hpp"
#endif

namespace {

volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int)
{
    g_stop = 1;
}

struct Options
{
    bool sim = false;
    std::string amp = "/dev/satlink-amp";
    std::uint16_t tc_port = satlink::payload::kTcPort;
    std::string gs_host;
    std::uint16_t gs_port = satlink::payload::kTmPort;
    bool tm_direct = false;
    bool tun = false;
    std::string can;
    std::uint8_t loopback = 2;
    std::uint8_t modcod = 1;
    bool acm = true;
    std::uint16_t scpi_port = 5025;
    std::string serial = "0";
    bool verbose = false;
};

Options Parse(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                throw std::invalid_argument(a + " needs a value");
            }
            return argv[++i];
        };
        if (a == "--sim")
            o.sim = true;
        else if (a == "--amp")
            o.amp = value();
        else if (a == "--tc-port")
            o.tc_port = static_cast<std::uint16_t>(std::stoul(value()));
        else if (a == "--gs")
        {
            const std::string v = value();
            const auto colon = v.find(':');
            o.gs_host = v.substr(0, colon);
            if (colon != std::string::npos)
            {
                o.gs_port = static_cast<std::uint16_t>(std::stoul(v.substr(colon + 1)));
            }
        }
        else if (a == "--tm-direct")
            o.tm_direct = true;
        else if (a == "--tun")
            o.tun = true;
        else if (a == "--can")
            o.can = value();
        else if (a == "--loopback")
        {
            const std::string v = value();
            o.loopback = v == "analog" ? 0 : (v == "digital" ? 1 : 2);
        }
        else if (a == "--modcod")
            o.modcod = static_cast<std::uint8_t>(std::stoul(value()));
        else if (a == "--no-acm")
            o.acm = false;
        else if (a == "--scpi-port")
            o.scpi_port = static_cast<std::uint16_t>(std::stoul(value()));
        else if (a == "--serial")
            o.serial = value();
        else if (a == "--verbose")
            o.verbose = true;
        else
            throw std::invalid_argument("unknown option " + a);
    }
    return o;
}

class BoardPlatform final : public satlink::payload::PlatformSource
{
  public:
    satlink::payload::CanPlatformSource *can = nullptr;
    std::function<void(satlink::payload::PlatformHk &)> amp_state;
    satlink::payload::PlatformHk Read() override
    {
        satlink::payload::PlatformHk hk =
            can != nullptr ? can->Read() : satlink::payload::PlatformHk{};
        if (amp_state)
        {
            amp_state(hk);
        }
        return hk;
    }
};

int Run(const Options &o)
{
    using namespace satlink;
    payload::SystemClock clock;
    payload::UdpGroundLink ground(o.tc_port, o.gs_host, o.gs_port);

    std::unique_ptr<amp::SimulatedCore1> sim;
    amp::MessagePort *port = nullptr;
    payload::PayloadConfig cfg;
    cfg.tm_direct = o.tm_direct;
    cfg.loopback = o.loopback;
    cfg.initial_modcod = o.modcod;
    cfg.acm = o.acm;
    BoardPlatform platform;
#if defined(SATLINK_HAVE_AMP_DEVICE)
    std::unique_ptr<amp::AmpDevice> device;
#endif
    if (o.sim)
    {
        sim = std::make_unique<amp::SimulatedCore1>();
        port = sim.get();
        cfg.loopback = SATLINK_LOOP_SOFTWARE;
    }
    else
    {
#if defined(SATLINK_HAVE_AMP_DEVICE)
        device = std::make_unique<amp::AmpDevice>(o.amp);
        port = device.get();
        amp::AmpDevice *dev = device.get();
        cfg.restart_modem = [dev]() {
            const int rc = dev->Stop();
            return rc != 0 ? rc : dev->Start();
        };
        platform.amp_state = [dev](payload::PlatformHk &hk) {
            satlink_amp_status st{};
            if (dev->GetStatus(st) == 0)
            {
                hk.rtos_state = static_cast<std::uint8_t>(st.state);
                hk.rtos_restarts = st.restarts;
            }
        };
#else
        throw std::runtime_error("no AMP device support in this build: use --sim");
#endif
    }

    std::unique_ptr<payload::TunDevice> sat_tun;
    std::unique_ptr<payload::TunDevice> gnd_tun;
    if (o.tun)
    {
        sat_tun = std::make_unique<payload::TunDevice>("satlink-sat");
        gnd_tun = std::make_unique<payload::TunDevice>("satlink-gnd");
    }
    std::unique_ptr<payload::CanPlatformSource> can;
    if (!o.can.empty())
    {
        can = std::make_unique<payload::CanPlatformSource>(
            std::make_unique<hal::SocketCanBus>(o.can));
        platform.can = can.get();
    }

    payload::PayloadManager manager(cfg, *port, ground, clock, sat_tun.get(), gnd_tun.get(),
                                    &platform);
    manager.Start();

    payload::ScpiInstrument instrument(
        manager, o.serial, std::string(SATLINK_VERSION_STRING "-") + SATLINK_GIT_REVISION);
    std::unique_ptr<payload::TcpLineServer> scpi;
    if (o.scpi_port != 0)
    {
        scpi = std::make_unique<payload::TcpLineServer>(
            o.scpi_port, [&instrument](std::string_view line) { return instrument.Execute(line); });
    }
    (void)std::fprintf(stderr, "satlink-payloadd: %s, TC on UDP %u, TM to %s:%u%s, SCPI %s\n",
                       o.sim ? "simulated modem" : o.amp.c_str(), o.tc_port,
                       o.gs_host.empty() ? "(last TC sender)" : o.gs_host.c_str(), o.gs_port,
                       o.tun ? ", IP on satlink-sat / satlink-gnd" : "",
                       scpi ? std::to_string(scpi->Port()).c_str() : "off");

    payload::SystemdNotifier systemd;
    (void)systemd.Notify("READY=1\nSTATUS=running");
    const std::uint64_t watchdog_ms = systemd.WatchdogMs() / 2U;

    std::vector<pollfd> base;
    base.push_back({ground.Fd(), POLLIN, 0});
#if defined(SATLINK_HAVE_AMP_DEVICE)
    if (device)
    {
        base.push_back({device->Fd(), POLLIN, 0});
    }
#endif
    if (sat_tun)
    {
        base.push_back({sat_tun->Fd(), POLLIN, 0});
        base.push_back({gnd_tun->Fd(), POLLIN, 0});
    }

    std::uint64_t last = clock.NowMs();
    std::uint64_t last_report = last;
    std::uint64_t last_watchdog = 0;
    std::vector<pollfd> fds;
    while (g_stop == 0)
    {
        fds = base;
        if (scpi)
        {
            scpi->AddPollFds(fds);
        }
        (void)::poll(fds.data(), fds.size(), 10);
        const std::uint64_t now = clock.NowMs();
        if (sim)
        {
            // Real-time link simulation; catch up at most 200 ms after a stall.
            sim->Advance(static_cast<std::uint32_t>(std::min<std::uint64_t>(now - last, 200)));
        }
        last = now;
        if (can)
        {
            can->Poll();
        }
        manager.Step();
        if (scpi)
        {
            scpi->Poll();
        }
        if (watchdog_ms != 0 && now - last_watchdog >= watchdog_ms)
        {
            last_watchdog = now;
            (void)systemd.Notify("WATCHDOG=1");
        }
        if (o.verbose && now - last_report >= 5000)
        {
            last_report = now;
            const auto s = manager.ModemStatus().value_or(satlink_msg_status_t{});
            const auto &st = manager.Stats();
            (void)std::fprintf(
                stderr,
                "lock=%u modcod=%u EsN0=%.1f dB frames tx=%u rx=%u crc=%u TC=%u TM=%u/%u "
                "pass=%s el=%.1f\n",
                s.locked, s.tx_modcod, s.esn0_cdb / 100.0, st.frames_sent, st.frames_received,
                st.frames_crc_error, st.tc_received, st.tm_delivered, st.tm_generated,
                manager.Pass().active ? "on" : "off", manager.Pass().sample.elevation_deg);
        }
    }
    (void)systemd.Notify("STOPPING=1");
    (void)std::fprintf(stderr, "satlink-payloadd: stopped\n");
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    (void)std::signal(SIGINT, OnSignal);
    (void)std::signal(SIGTERM, OnSignal);
    try
    {
        return Run(Parse(argc, argv));
    }
    catch (const std::exception &e)
    {
        (void)std::fprintf(stderr, "satlink-payloadd: %s\n", e.what());
        return 1;
    }
}
