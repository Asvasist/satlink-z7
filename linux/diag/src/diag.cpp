/**
 * @file diag.cpp
 * @implements SRS-DIAG-001
 */
#include "satlink/diag/diag.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "satlink/hal/ccsds_frame_accel.hpp"
#include "satlink/hal/spec_tap.hpp"
#include "satlink/hal/ssm2603.hpp"

namespace satlink::diag {
namespace {

constexpr const char *kUsage =
    "usage: satlink-diag <block> <command> [options] [--dev PATH]\n"
    "\n"
    "  fa version                            frame accelerator version\n"
    "  fa ctrl [randomizer=on|off] [irq=on|off]\n"
    "                                        show or change the control bits\n"
    "  fa wait [timeout_ms=N]                wait for the next frame (default 1000 ms)\n"
    "  spec version                          spectrum tap version\n"
    "  spec ctrl [enable=on|off] [window=on|off]\n"
    "                                        show or change the control bits\n"
    "  spec status                           busy flag, dropped-frame flag, frame count\n"
    "  spec clear                            clear the dropped-frame flag\n"
    "  codec init [wordlength=16|20|24|32]   reset and configure the audio codec\n"
    "  codec volume <0-127>                  headphone volume (121 = 0 dB)\n"
    "  codec mute on|off                     mute the DAC\n"
    "  help                                  this text\n"
    "\n"
    "--dev overrides the device node: the char device for fa/spec, the i2c-dev node for codec.\n"
    "Exit codes: 0 ok, 1 device error, 2 usage error, 3 timeout.\n";

struct Options
{
    std::string dev;
    std::vector<std::string> positional;
    std::map<std::string, std::string> keyvals;
};

Options ParseOptions(const std::vector<std::string> &args, std::size_t first)
{
    Options options;
    for (std::size_t i = first; i < args.size(); ++i)
    {
        const std::string &arg = args[i];
        if (arg == "--dev")
        {
            ++i;
            if (i >= args.size())
            {
                throw std::invalid_argument("--dev needs a path");
            }
            options.dev = args[i];
            continue;
        }

        const std::size_t equals = arg.find('=');
        if (equals == std::string::npos)
        {
            options.positional.push_back(arg);
        }
        else if (!options.keyvals.emplace(arg.substr(0, equals), arg.substr(equals + 1)).second)
        {
            throw std::invalid_argument("option '" + arg.substr(0, equals) + "' given twice");
        }
    }
    return options;
}

std::optional<std::string> TakeOption(Options &options, const std::string &key)
{
    const auto found = options.keyvals.find(key);
    if (found == options.keyvals.end())
    {
        return std::nullopt;
    }
    std::string value = std::move(found->second);
    options.keyvals.erase(found);
    return value;
}

std::string TakePositional(Options &options, const char *what)
{
    if (options.positional.empty())
    {
        throw std::invalid_argument(std::string("missing ") + what);
    }
    std::string value = std::move(options.positional.front());
    options.positional.erase(options.positional.begin());
    return value;
}

void RejectLeftovers(const Options &options)
{
    if (!options.positional.empty())
    {
        throw std::invalid_argument("unexpected argument '" + options.positional.front() + "'");
    }
    if (!options.keyvals.empty())
    {
        throw std::invalid_argument("unknown option '" + options.keyvals.begin()->first + "'");
    }
}

unsigned ParseUnsigned(const std::string &text, unsigned max, const char *what)
{
    unsigned value = 0;
    const char *const first = text.data();
    const char *const last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last || value > max)
    {
        throw std::invalid_argument(std::string(what) + " must be a number from 0 to " +
                                    std::to_string(max));
    }
    return value;
}

bool ParseOnOff(const std::string &text, const char *what)
{
    if (text == "on")
    {
        return true;
    }
    if (text == "off")
    {
        return false;
    }
    throw std::invalid_argument(std::string(what) + " must be on or off");
}

const char *OnOff(bool value)
{
    return value ? "on" : "off";
}

int RunFrameAccel(const std::string &command, Options &options, DeviceFactory &factory,
                  std::ostream &out)
{
    if (command != "version" && command != "ctrl" && command != "wait")
    {
        throw std::invalid_argument("unknown fa command '" + command + "'");
    }

    std::optional<bool> randomizer;
    std::optional<bool> irq;
    unsigned timeout_ms = 1000;
    if (command == "ctrl")
    {
        if (const auto text = TakeOption(options, "randomizer"))
        {
            randomizer = ParseOnOff(*text, "randomizer");
        }
        if (const auto text = TakeOption(options, "irq"))
        {
            irq = ParseOnOff(*text, "irq");
        }
    }
    else if (command == "wait")
    {
        if (const auto text = TakeOption(options, "timeout_ms"))
        {
            timeout_ms = ParseUnsigned(*text, 3600000U, "timeout_ms");
        }
    }
    RejectLeftovers(options);

    const auto io = factory.OpenCharDevice(options.dev.empty() ? kFrameAccelPath : options.dev);
    const hal::FrameAccelerator accel(*io);

    if (command == "version")
    {
        const hal::FrameAccelVersion version = accel.GetVersion();
        out << "ccsds_frame_accel version " << version.major << '.' << version.minor << '\n';
        return kExitOk;
    }

    if (command == "ctrl")
    {
        hal::FrameAccelCtrl ctrl = accel.GetCtrl();
        if (randomizer || irq)
        {
            ctrl.randomizer_en = randomizer.value_or(ctrl.randomizer_en);
            ctrl.irq_en = irq.value_or(ctrl.irq_en);
            accel.SetCtrl(ctrl);
            ctrl = accel.GetCtrl();
        }
        out << "randomizer=" << OnOff(ctrl.randomizer_en) << " irq=" << OnOff(ctrl.irq_en) << '\n';
        return kExitOk;
    }

    const std::optional<hal::FrameStats> stats =
        accel.WaitFrame(std::chrono::milliseconds(timeout_ms));
    if (!stats)
    {
        out << "timeout after " << timeout_ms << " ms\n";
        return kExitTimeout;
    }
    out << "frame: crc=0x" << std::hex << std::setw(4) << std::setfill('0') << stats->crc
        << std::dec << std::setfill(' ') << " length=" << stats->last_len_bytes
        << " count=" << stats->frame_cnt << '\n';
    return kExitOk;
}

int RunSpecTap(const std::string &command, Options &options, DeviceFactory &factory,
               std::ostream &out)
{
    if (command != "version" && command != "ctrl" && command != "status" && command != "clear")
    {
        throw std::invalid_argument("unknown spec command '" + command + "'");
    }

    std::optional<bool> enable;
    std::optional<bool> window;
    if (command == "ctrl")
    {
        if (const auto text = TakeOption(options, "enable"))
        {
            enable = ParseOnOff(*text, "enable");
        }
        if (const auto text = TakeOption(options, "window"))
        {
            window = ParseOnOff(*text, "window");
        }
    }
    RejectLeftovers(options);

    const auto io = factory.OpenCharDevice(options.dev.empty() ? kSpecTapPath : options.dev);
    const hal::SpecTap tap(*io);

    if (command == "version")
    {
        const hal::SpecTapVersion version = tap.GetVersion();
        out << "spec_tap version " << version.major << '.' << version.minor << '\n';
    }
    else if (command == "ctrl")
    {
        hal::SpecTapCtrl ctrl = tap.GetCtrl();
        if (enable || window)
        {
            ctrl.enable = enable.value_or(ctrl.enable);
            ctrl.window_en = window.value_or(ctrl.window_en);
            tap.SetCtrl(ctrl);
            ctrl = tap.GetCtrl();
        }
        out << "enable=" << OnOff(ctrl.enable) << " window=" << OnOff(ctrl.window_en) << '\n';
    }
    else if (command == "status")
    {
        const hal::SpecTapStatus status = tap.GetStatus();
        out << "busy=" << OnOff(status.busy) << " dropped=" << OnOff(status.frame_dropped)
            << " frames=" << status.frame_cnt << '\n';
    }
    else
    {
        tap.ClearFrameDropped();
        out << "dropped-frame flag cleared\n";
    }
    return kExitOk;
}

hal::Ssm2603WordLength ToWordLength(unsigned bits)
{
    switch (bits)
    {
    case 16:
        return hal::Ssm2603WordLength::k16Bit;
    case 20:
        return hal::Ssm2603WordLength::k20Bit;
    case 24:
        return hal::Ssm2603WordLength::k24Bit;
    case 32:
        return hal::Ssm2603WordLength::k32Bit;
    default:
        throw std::invalid_argument("wordlength must be 16, 20, 24 or 32");
    }
}

int RunCodec(const std::string &command, Options &options, DeviceFactory &factory,
             std::ostream &out)
{
    if (command != "init" && command != "volume" && command != "mute")
    {
        throw std::invalid_argument("unknown codec command '" + command + "'");
    }

    hal::Ssm2603Config config;
    std::uint8_t volume = 0;
    bool mute = false;
    if (command == "init")
    {
        if (const auto text = TakeOption(options, "wordlength"))
        {
            config.word_length = ToWordLength(ParseUnsigned(*text, 32U, "wordlength"));
        }
    }
    else if (command == "volume")
    {
        volume = static_cast<std::uint8_t>(ParseUnsigned(
            TakePositional(options, "volume"), hal::Ssm2603::kHeadphoneVolumeMax, "volume"));
    }
    else
    {
        mute = ParseOnOff(TakePositional(options, "on or off"), "mute");
    }
    RejectLeftovers(options);

    const auto bus = factory.OpenI2c(options.dev.empty() ? kCodecBusPath : options.dev,
                                     hal::Ssm2603::kI2cAddress);
    hal::Ssm2603 codec(*bus);

    if (command == "init")
    {
        codec.Initialize(config);
        out << "codec initialised\n";
        return kExitOk;
    }

    // The codec cannot be read back and each run is a new process, so assume it was brought up
    // with "codec init" and its defaults.
    codec.AssumeConfigured();
    if (command == "volume")
    {
        codec.SetHeadphoneVolume(volume);
        out << "headphone volume set to " << static_cast<unsigned>(volume) << '\n';
    }
    else
    {
        codec.MuteDac(mute);
        out << "DAC " << (mute ? "muted" : "unmuted") << '\n';
    }
    return kExitOk;
}

} // namespace

int RunDiag(const std::vector<std::string> &args, DeviceFactory &factory, std::ostream &out,
            std::ostream &err)
{
    if (args.empty())
    {
        err << kUsage;
        return kExitUsageError;
    }
    if (args[0] == "help" || args[0] == "--help" || args[0] == "-h")
    {
        out << kUsage;
        return kExitOk;
    }

    try
    {
        if (args.size() < 2)
        {
            throw std::invalid_argument("missing command");
        }
        Options options = ParseOptions(args, 2);
        if (args[0] == "fa")
        {
            return RunFrameAccel(args[1], options, factory, out);
        }
        if (args[0] == "spec")
        {
            return RunSpecTap(args[1], options, factory, out);
        }
        if (args[0] == "codec")
        {
            return RunCodec(args[1], options, factory, out);
        }
        throw std::invalid_argument("unknown block '" + args[0] + "'");
    }
    catch (const std::invalid_argument &error)
    {
        err << "error: " << error.what() << "\n\n" << kUsage;
        return kExitUsageError;
    }
    catch (const std::out_of_range &error)
    {
        err << "error: " << error.what() << "\n\n" << kUsage;
        return kExitUsageError;
    }
    catch (const std::exception &error)
    {
        err << "error: " << error.what() << '\n';
        return kExitRuntimeError;
    }
}

} // namespace satlink::diag
