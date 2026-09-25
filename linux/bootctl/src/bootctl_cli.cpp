/**
 * @file bootctl_cli.cpp
 * @implements SRS-BOOT-002
 */
#include "satlink/bootctl/bootctl_cli.hpp"

#include <exception>
#include <ostream>

namespace satlink::bootctl {
namespace {

constexpr const char *kUsage =
    "usage: satlink-bootctl <command>\n"
    "\n"
    "  status           slots, trial state and what U-Boot will boot next\n"
    "  inactive         device of the slot that is not running (the update target)\n"
    "  prepare-update   put the inactive slot on trial for the next boot\n"
    "  mark-good        confirm the running slot unconditionally\n"
    "  confirm          confirm the running slot if the system is healthy; exit 4 otherwise\n"
    "                   (satlink-boot-ok.service reboots on that, so U-Boot counts the boot)\n";

} // namespace

int RunBootctl(const std::vector<std::string> &args, CliContext &ctx, std::ostream &out,
               std::ostream &err)
{
    if (args.size() != 1)
    {
        err << kUsage;
        return kExitUsage;
    }
    const std::string &cmd = args[0];
    BootControl control(ctx.env, ctx.cmdline);
    try
    {
        if (cmd == "status")
        {
            const auto status = control.ReadStatus();
            const auto next = BootControl::PredictNextBoot(status);
            out << "running:    "
                << (status.running_slot ? SlotName(*status.running_slot) : "golden") << "\n"
                << "boot_slot:  " << SlotName(status.boot_slot) << "\n"
                << "state:      " << StateName(status.state) << "\n"
                << "bootcount:  " << status.bootcount << "/" << status.bootlimit << "\n"
                << "next boot:  "
                << (next.kind == NextBoot::Kind::kGolden
                        ? std::string("golden")
                        : std::string("slot ") + SlotName(next.slot))
                << "\n";
            return kExitOk;
        }
        if (cmd == "inactive")
        {
            const auto status = control.ReadStatus();
            out << SlotDevice(OtherSlot(status.running_slot.value_or(status.boot_slot))) << "\n";
            return kExitOk;
        }
        if (cmd == "prepare-update")
        {
            const Slot target = control.PrepareUpdate();
            out << "slot " << SlotName(target) << " (" << SlotDevice(target)
                << ") will be tried at the next boot\n";
            return kExitOk;
        }
        if (cmd == "mark-good")
        {
            control.MarkGood();
            out << "ok\n";
            return kExitOk;
        }
        if (cmd == "confirm")
        {
            const auto status = control.ReadStatus();
            if (status.state == BootState::kNormal)
            {
                out << "nothing to confirm\n";
                return kExitOk;
            }
            if (!ctx.healthy())
            {
                err << "satlink-bootctl: system not healthy, slot "
                    << (status.running_slot ? SlotName(*status.running_slot) : "?")
                    << " stays on trial\n";
                return kExitUnhealthy;
            }
            control.MarkGood();
            out << "slot confirmed\n";
            return kExitOk;
        }
    }
    catch (const std::exception &e)
    {
        err << "satlink-bootctl: " << e.what() << "\n";
        return kExitError;
    }
    err << "satlink-bootctl: unknown command '" << cmd << "'\n" << kUsage;
    return kExitUsage;
}

} // namespace satlink::bootctl
