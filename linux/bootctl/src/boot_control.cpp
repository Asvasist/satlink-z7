/**
 * @file boot_control.cpp
 * @implements SRS-BOOT-002
 */
#include "satlink/bootctl/boot_control.hpp"

#include <charconv>
#include <sstream>

namespace satlink::bootctl {
namespace {

int ToInt(const std::map<std::string, std::string> &env, const std::string &key, int fallback)
{
    const auto it = env.find(key);
    if (it == env.end())
    {
        return fallback;
    }
    int value = fallback;
    const auto *end = it->second.data() + it->second.size();
    const auto [ptr, ec] = std::from_chars(it->second.data(), end, value);
    return (ec == std::errc{} && ptr == end) ? value : fallback;
}

std::optional<Slot> ParseSlot(const std::string &text)
{
    if (text == "a")
    {
        return Slot::kA;
    }
    if (text == "b")
    {
        return Slot::kB;
    }
    return std::nullopt;
}

} // namespace

const char *SlotName(Slot slot)
{
    return slot == Slot::kA ? "a" : "b";
}

Slot OtherSlot(Slot slot)
{
    return slot == Slot::kA ? Slot::kB : Slot::kA;
}

const char *SlotDevice(Slot slot)
{
    return slot == Slot::kA ? "/dev/mmcblk0p2" : "/dev/mmcblk0p3";
}

const char *StateName(BootState state)
{
    switch (state)
    {
    case BootState::kNormal:
        return "normal";
    case BootState::kTrial:
        return "trial";
    case BootState::kRolledBack:
        return "rolled-back";
    }
    return "?";
}

std::optional<Slot> BootControl::RunningSlot() const
{
    std::istringstream words(cmdline_);
    std::string word;
    while (words >> word)
    {
        if (word.starts_with("satlink.slot="))
        {
            return ParseSlot(word.substr(13));
        }
    }
    return std::nullopt;
}

Status BootControl::ReadStatus()
{
    const auto env = env_.Read();
    Status status;
    const auto slot = env.find("boot_slot");
    status.boot_slot = (slot != env.end()) ? ParseSlot(slot->second).value_or(Slot::kA) : Slot::kA;
    status.running_slot = RunningSlot();
    status.bootcount = ToInt(env, "bootcount", 0);
    status.bootlimit = ToInt(env, "bootlimit", 3);
    const bool trial = ToInt(env, "upgrade_available", 0) == 1;
    const bool rollback = ToInt(env, "rollback", 0) == 1;
    status.state =
        !trial ? BootState::kNormal : (rollback ? BootState::kRolledBack : BootState::kTrial);
    return status;
}

void BootControl::MarkGood()
{
    const auto status = ReadStatus();
    if (!status.running_slot)
    {
        throw BootControlError("running from the golden image: nothing to confirm");
    }
    if (status.state == BootState::kNormal && status.boot_slot == *status.running_slot)
    {
        return;
    }
    env_.Write({{"boot_slot", SlotName(*status.running_slot)},
                {"upgrade_available", "0"},
                {"bootcount", "0"},
                {"rollback", "0"}});
}

Slot BootControl::PrepareUpdate()
{
    const auto status = ReadStatus();
    if (status.state != BootState::kNormal)
    {
        throw BootControlError(std::string("a boot trial is in progress (") +
                               StateName(status.state) + "): confirm it first");
    }
    const Slot running = status.running_slot.value_or(status.boot_slot);
    const Slot target = OtherSlot(running);
    env_.Write({{"boot_slot", SlotName(target)},
                {"upgrade_available", "1"},
                {"bootcount", "0"},
                {"rollback", "0"}});
    return target;
}

NextBoot BootControl::PredictNextBoot(const Status &status)
{
    // U-Boot increments bootcount before choosing bootcmd or altbootcmd, and only while
    // upgrade_available is 1 (CONFIG_BOOTCOUNT_LIMIT).
    const bool counting = status.state != BootState::kNormal;
    const int count = counting ? status.bootcount + 1 : 0;
    if (!counting || count <= status.bootlimit)
    {
        return {NextBoot::Kind::kSlot, status.boot_slot};
    }
    if (status.state == BootState::kRolledBack)
    {
        return {NextBoot::Kind::kGolden, status.boot_slot};
    }
    return {NextBoot::Kind::kSlot, OtherSlot(status.boot_slot)};
}

} // namespace satlink::bootctl
