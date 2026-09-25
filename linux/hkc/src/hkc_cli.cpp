/**
 * @file hkc_cli.cpp
 * @implements SRS-HKC-006
 */
#include "satlink/hkc/hkc_cli.hpp"

#include <cerrno>
#include <charconv>
#include <ostream>
#include <stdexcept>
#include <system_error>

#include "satlink/hkc/hkc_client.hpp"

namespace satlink::hkc {
namespace {

constexpr const char *kUsage =
    "usage: satlink-hkc [--if IFACE] <command> [args]\n"
    "\n"
    "Bootloader:\n"
    "  ping                      bootloader version and whether it holds a valid application\n"
    "  flash FILE [--boot]       download an image made by tools/hkc/mkimage.py\n"
    "  boot                      start the application held by the bootloader\n"
    "Application:\n"
    "  version                   application version\n"
    "  led <0-7>                 set RGB LED6 (bit0 red, bit1 green, bit2 blue)\n"
    "  period <100-10000>        housekeeping period in ms\n"
    "  clear-errors              clear the sticky error flags\n"
    "  enter-boot                reset into the bootloader and stay there\n"
    "  time-sync                 send the current time\n"
    "  monitor [count=N] [timeout_ms=N]\n"
    "                            print decoded housekeeping frames\n"
    "\n"
    "Default interface: can0. Exit codes: 0 ok, 1 error, 2 usage error, 3 timeout.\n";

class UsageError : public std::invalid_argument
{
  public:
    using std::invalid_argument::invalid_argument;
};

unsigned long ParseNumber(const std::string &text, unsigned long max, const char *what)
{
    unsigned long value = 0;
    const auto *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || value > max)
    {
        throw UsageError(std::string(what) + ": expected a number up to " + std::to_string(max));
    }
    return value;
}

int CheckAck(const satlink_hk_ack_t &ack, std::ostream &out, std::ostream &err)
{
    if (ack.status == SATLINK_HK_ACK_OK)
    {
        out << "ok\n";
        return kExitOk;
    }
    err << "satlink-hkc: command rejected ("
        << (ack.status == SATLINK_HK_ACK_BAD_ARG ? "bad argument" : "unknown command") << ")\n";
    return kExitRuntimeError;
}

int Monitor(hal::CanBus &bus, const std::vector<std::string> &args, std::ostream &out)
{
    unsigned long count = 0; // 0: forever
    unsigned long timeout_ms = 5000;
    for (const auto &arg : args)
    {
        if (arg.starts_with("count="))
        {
            count = ParseNumber(arg.substr(6), 1000000, "count");
        }
        else if (arg.starts_with("timeout_ms="))
        {
            timeout_ms = ParseNumber(arg.substr(11), 3600000, "timeout_ms");
        }
        else
        {
            throw UsageError("unknown monitor option '" + arg + "'");
        }
    }
    unsigned long printed = 0;
    while (count == 0 || printed < count)
    {
        satlink_can_frame_t frame{};
        const int rc = bus.Receive(frame, std::chrono::milliseconds{timeout_ms});
        if (rc == -ETIMEDOUT)
        {
            throw HkcTimeout("no housekeeping frames");
        }
        if (rc != 0)
        {
            throw std::system_error(-rc, std::generic_category(), "CAN receive");
        }
        if (const auto text = DescribeFrame(frame))
        {
            out << *text << '\n';
            ++printed;
        }
    }
    return kExitOk;
}

int Dispatch(const std::vector<std::string> &args, const std::string &interface, Environment &env,
             std::ostream &out, std::ostream &err)
{
    const std::string &cmd = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    auto expect_args = [&](std::size_t n) {
        if (rest.size() != n)
        {
            throw UsageError("'" + cmd + "' takes " + std::to_string(n) + " argument(s)");
        }
    };

    if (cmd == "ping" || cmd == "boot" || cmd == "version" || cmd == "clear-errors" ||
        cmd == "enter-boot" || cmd == "time-sync")
    {
        expect_args(0);
    }
    else if (cmd == "led" || cmd == "period")
    {
        expect_args(1);
    }
    else if (cmd == "flash")
    {
        if (rest.empty() || rest.size() > 2 || (rest.size() == 2 && rest[1] != "--boot"))
        {
            throw UsageError("usage: flash FILE [--boot]");
        }
    }
    else if (cmd != "monitor")
    {
        throw UsageError("unknown command '" + cmd + "'");
    }

    // Parse arguments before touching the bus.
    std::vector<std::uint8_t> image;
    std::vector<std::uint8_t> cmd_args;
    if (cmd == "led")
    {
        cmd_args.push_back(static_cast<std::uint8_t>(ParseNumber(rest[0], 7, "led")));
    }
    else if (cmd == "period")
    {
        const auto ms = ParseNumber(rest[0], 10000, "period");
        cmd_args = {static_cast<std::uint8_t>(ms & 0xFFU), static_cast<std::uint8_t>(ms >> 8U)};
    }
    else if (cmd == "flash")
    {
        image = env.ReadFile(rest[0]);
    }

    auto bus = env.OpenCan(interface);
    HkcClient client(*bus);

    if (cmd == "ping")
    {
        const auto info = client.Ping();
        out << "bootloader " << int{info.version_major} << "." << int{info.version_minor}
            << ", application " << (info.app_valid ? "valid" : "absent") << "\n";
        return kExitOk;
    }
    if (cmd == "flash")
    {
        int last_percent = -1;
        client.Flash(image, [&](std::size_t done, std::size_t total) {
            const int percent = static_cast<int>((done * 100U) / total);
            if (percent / 10 != last_percent / 10)
            {
                out << "flash: " << percent << "%\n";
                last_percent = percent;
            }
        });
        out << "flash: " << image.size() << " bytes verified\n";
        if (rest.size() == 2)
        {
            client.Boot();
            out << "application started\n";
        }
        return kExitOk;
    }
    if (cmd == "boot")
    {
        client.Boot();
        out << "application started\n";
        return kExitOk;
    }
    if (cmd == "version")
    {
        const auto ack = client.Command(SATLINK_HK_CMD_GET_VERSION);
        if (ack.status != SATLINK_HK_ACK_OK || ack.datac < 3)
        {
            return CheckAck(ack, out, err);
        }
        out << "application " << int{ack.datav[0]} << "." << int{ack.datav[1]} << "."
            << int{ack.datav[2]} << "\n";
        return kExitOk;
    }
    if (cmd == "led")
    {
        return CheckAck(client.Command(SATLINK_HK_CMD_SET_LED, cmd_args), out, err);
    }
    if (cmd == "period")
    {
        return CheckAck(client.Command(SATLINK_HK_CMD_SET_PERIOD, cmd_args), out, err);
    }
    if (cmd == "clear-errors")
    {
        return CheckAck(client.Command(SATLINK_HK_CMD_CLEAR_ERRORS), out, err);
    }
    if (cmd == "enter-boot")
    {
        const std::uint8_t key[2] = {0xB0, 0x07};
        return CheckAck(client.Command(SATLINK_HK_CMD_ENTER_BOOT, key), out, err);
    }
    if (cmd == "time-sync")
    {
        client.SyncTime(env.Now());
        out << "ok\n";
        return kExitOk;
    }
    return Monitor(*bus, rest, out);
}

} // namespace

int RunHkc(const std::vector<std::string> &args, Environment &env, std::ostream &out,
           std::ostream &err)
{
    std::string interface = kDefaultInterface;
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--if")
        {
            if (i + 1 >= args.size())
            {
                err << "satlink-hkc: --if needs an interface name\n" << kUsage;
                return kExitUsageError;
            }
            interface = args[++i];
        }
        else
        {
            rest.push_back(args[i]);
        }
    }
    if (rest.empty() || rest[0] == "help" || rest[0] == "--help")
    {
        (rest.empty() ? err : out) << kUsage;
        return rest.empty() ? kExitUsageError : kExitOk;
    }

    try
    {
        return Dispatch(rest, interface, env, out, err);
    }
    catch (const UsageError &e)
    {
        err << "satlink-hkc: " << e.what() << "\n" << kUsage;
        return kExitUsageError;
    }
    catch (const HkcTimeout &e)
    {
        err << "satlink-hkc: " << e.what() << "\n";
        return kExitTimeout;
    }
    catch (const std::exception &e)
    {
        err << "satlink-hkc: " << e.what() << "\n";
        return kExitRuntimeError;
    }
}

} // namespace satlink::hkc
