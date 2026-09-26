/**
 * @file boot_control.hpp
 * @brief A/B slot bookkeeping shared with U-Boot through its environment.
 *
 * U-Boot (yocto/meta-satlink/recipes-bsp/u-boot/files/satlink-env.txt) and Linux agree on
 * these variables:
 *
 * | Variable            | Meaning                                                            |
 * |---------------------|--------------------------------------------------------------------|
 * | boot_slot           | Slot U-Boot boots: "a" (mmcblk0p2) or "b" (mmcblk0p3)              |
 * | upgrade_available   | 1 while a slot is on trial: U-Boot counts boots in bootcount       |
 * | bootcount           | Boots since the trial started (CONFIG_BOOTCOUNT_ENV)               |
 * | bootlimit           | Boots allowed before U-Boot runs altbootcmd (3)                    |
 * | rollback            | 1 after altbootcmd switched back to the other slot                 |
 *
 * altbootcmd switches to the other slot the first time the limit is hit and boots the golden
 * image the second time. Linux ends a trial with MarkGood() once the system is healthy.
 *
 * @implements SRS-BOOT-002
 */
#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace satlink::bootctl {

/// Access to the U-Boot environment (fw_printenv / fw_setenv on the target).
class EnvStore
{
  public:
    virtual ~EnvStore() = default;
    virtual std::map<std::string, std::string> Read() = 0;
    /// Applies all @p updates in one write (a partial update must never reach the SD card).
    virtual void Write(const std::map<std::string, std::string> &updates) = 0;
};

enum class Slot
{
    kA,
    kB,
};

enum class BootState
{
    kNormal,     ///< No trial: booting a confirmed slot.
    kTrial,      ///< A new slot is on trial.
    kRolledBack, ///< The trial failed; the previous slot is booting (and counted).
};

struct Status
{
    Slot boot_slot = Slot::kA;
    std::optional<Slot>
        running_slot; ///< From the kernel command line; nullopt in the golden image.
    BootState state = BootState::kNormal;
    int bootcount = 0;
    int bootlimit = 3;
};

/// What U-Boot will do at the next reset if Linux does not intervene.
struct NextBoot
{
    enum class Kind
    {
        kSlot,
        kGolden,
    };
    Kind kind = Kind::kSlot;
    Slot slot = Slot::kA;
};

class BootControlError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

const char *SlotName(Slot slot);
Slot OtherSlot(Slot slot);
/// Root partition device of a slot.
const char *SlotDevice(Slot slot);
const char *StateName(BootState state);

class BootControl
{
  public:
    /// @p cmdline is the kernel command line (contents of /proc/cmdline).
    BootControl(EnvStore &env, std::string cmdline) : env_(env), cmdline_(std::move(cmdline)) {}

    Status ReadStatus();

    /// Ends a trial (or a rollback): the running slot becomes the confirmed one. No-op in
    /// kNormal. Throws BootControlError when running from the golden image.
    void MarkGood();

    /// Puts the slot that is not running on trial for the next boot. The caller must already
    /// have written a complete root file system to SlotDevice(target). Throws BootControlError
    /// when a trial is in progress (confirm or roll back first).
    Slot PrepareUpdate();

    /// Model of the U-Boot boot logic: what happens at the next reset, given @p status. Used by
    /// "satlink-bootctl status" and by the host tests that pin down the U-Boot state machine.
    static NextBoot PredictNextBoot(const Status &status);

  private:
    std::optional<Slot> RunningSlot() const;

    EnvStore &env_;
    std::string cmdline_;
};

} // namespace satlink::bootctl
