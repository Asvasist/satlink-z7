/**
 * @file test_boot_control.cpp
 * @brief A/B boot: satlink-bootctl against an in-memory U-Boot environment, and whole update
 *        scenarios (good update, bad update with rollback, rollback slot failing too) run
 *        through a simulation of the U-Boot boot logic in satlink-env.txt.
 *
 * @verifies SRS-BOOT-002
 * @verifies SRS-BOOT-003
 */
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <string>

#include "satlink/bootctl/boot_control.hpp"
#include "satlink/bootctl/bootctl_cli.hpp"

namespace satlink::bootctl {
namespace {

class MemEnv : public EnvStore
{
  public:
    std::map<std::string, std::string> Read() override
    {
        return vars;
    }
    void Write(const std::map<std::string, std::string> &updates) override
    {
        ++writes;
        for (const auto &[k, v] : updates)
        {
            vars[k] = v;
        }
    }
    std::map<std::string, std::string> vars{{"boot_slot", "a"},
                                            {"bootlimit", "3"},
                                            {"bootcount", "0"},
                                            {"upgrade_available", "0"},
                                            {"rollback", "0"}};
    int writes = 0;
};

/// What a reset does, as written in satlink-env.txt (bootcmd, altbootcmd, CONFIG_BOOTCOUNT_ENV).
/// Returns the kernel command line of the system that comes up.
std::string SimulateReset(MemEnv &env, bool slot_a_signed_ok = true, bool slot_b_signed_ok = true)
{
    auto &v = env.vars;
    const bool counting = v["upgrade_available"] == "1";
    if (counting)
    {
        v["bootcount"] = std::to_string(std::stoi(v["bootcount"]) + 1);
    }
    auto boot_slot = [&]() -> std::string {
        const bool ok = v["boot_slot"] == "a" ? slot_a_signed_ok : slot_b_signed_ok;
        return ok ? "satlink.slot=" + v["boot_slot"] : "satlink.slot=golden";
    };
    if (!counting || std::stoi(v["bootcount"]) <= std::stoi(v["bootlimit"]))
    {
        return boot_slot(); // bootcmd
    }
    // altbootcmd
    if (v["rollback"] == "1")
    {
        return "satlink.slot=golden";
    }
    v["boot_slot"] = v["boot_slot"] == "a" ? "b" : "a";
    v["rollback"] = "1";
    v["bootcount"] = "0";
    return boot_slot();
}

std::string Expected(const NextBoot &next)
{
    return next.kind == NextBoot::Kind::kGolden
               ? "satlink.slot=golden"
               : std::string("satlink.slot=") + SlotName(next.slot);
}

/// Resets once and checks that PredictNextBoot() agreed with the simulated U-Boot.
std::string Reset(MemEnv &env, const std::string &cmdline)
{
    BootControl control(env, cmdline);
    const auto predicted = Expected(BootControl::PredictNextBoot(control.ReadStatus()));
    const auto actual = SimulateReset(env);
    EXPECT_EQ(predicted, actual);
    return actual;
}

TEST(BootControlTest, StatusParsesEnvironmentAndCommandLine)
{
    MemEnv env;
    env.vars["boot_slot"] = "b";
    env.vars["upgrade_available"] = "1";
    env.vars["bootcount"] = "2";
    BootControl control(env, "console=ttyPS0,115200 root=/dev/mmcblk0p3 satlink.slot=b rootwait");
    const auto s = control.ReadStatus();
    EXPECT_EQ(Slot::kB, s.boot_slot);
    ASSERT_TRUE(s.running_slot.has_value());
    EXPECT_EQ(Slot::kB, s.running_slot.value_or(Slot::kA));
    EXPECT_EQ(BootState::kTrial, s.state);
    EXPECT_EQ(2, s.bootcount);
    EXPECT_EQ(3, s.bootlimit);
}

TEST(BootControlTest, GarbageValuesFallBackToSafeDefaults)
{
    MemEnv env;
    env.vars["boot_slot"] = "z";
    env.vars["bootcount"] = "x1";
    env.vars.erase("bootlimit");
    BootControl control(env, "");
    const auto s = control.ReadStatus();
    EXPECT_EQ(Slot::kA, s.boot_slot);
    EXPECT_FALSE(s.running_slot.has_value());
    EXPECT_EQ(0, s.bootcount);
    EXPECT_EQ(3, s.bootlimit);
}

TEST(BootControlTest, SuccessfulUpdate)
{
    MemEnv env;
    BootControl running_a(env, "satlink.slot=a");
    EXPECT_EQ(Slot::kB, running_a.PrepareUpdate());
    EXPECT_EQ(1, env.writes); // one atomic write

    const auto cmdline = Reset(env, "satlink.slot=a");
    EXPECT_EQ("satlink.slot=b", cmdline);
    BootControl running_b(env, cmdline);
    EXPECT_EQ(BootState::kTrial, running_b.ReadStatus().state);
    running_b.MarkGood();
    EXPECT_EQ(BootState::kNormal, running_b.ReadStatus().state);
    EXPECT_EQ("b", env.vars["boot_slot"]);

    // Confirmed: further resets are not counted.
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ("satlink.slot=b", Reset(env, "satlink.slot=b"));
    }
    EXPECT_EQ("0", env.vars["bootcount"]);
}

TEST(BootControlTest, BadUpdateRollsBackAfterBootlimit)
{
    MemEnv env;
    BootControl(env, "satlink.slot=a").PrepareUpdate();
    std::string cmdline = "satlink.slot=a";
    for (int i = 0; i < 3; ++i) // new slot keeps failing before it confirms itself
    {
        cmdline = Reset(env, cmdline);
        EXPECT_EQ("satlink.slot=b", cmdline);
    }
    cmdline = Reset(env, cmdline);
    EXPECT_EQ("satlink.slot=a", cmdline); // fourth boot: altbootcmd switched back

    BootControl control(env, cmdline);
    EXPECT_EQ(BootState::kRolledBack, control.ReadStatus().state);
    control.MarkGood();
    const auto s = control.ReadStatus();
    EXPECT_EQ(BootState::kNormal, s.state);
    EXPECT_EQ(Slot::kA, s.boot_slot);
}

TEST(BootControlTest, BothSlotsFailingEndsInGoldenImage)
{
    MemEnv env;
    BootControl(env, "satlink.slot=a").PrepareUpdate();
    std::string cmdline = "satlink.slot=a";
    for (int i = 0; i < 7; ++i)
    {
        cmdline = Reset(env, cmdline);
    }
    EXPECT_EQ("satlink.slot=golden", Reset(env, cmdline));
    BootControl golden(env, "satlink.slot=golden");
    EXPECT_THROW(golden.MarkGood(), BootControlError);
}

TEST(BootControlTest, PrepareUpdateRefusesDuringTrial)
{
    MemEnv env;
    BootControl control(env, "satlink.slot=a");
    control.PrepareUpdate();
    EXPECT_THROW(control.PrepareUpdate(), BootControlError);
}

TEST(BootControlTest, MarkGoodWhenAlreadyNormalWritesNothing)
{
    MemEnv env;
    BootControl(env, "satlink.slot=a").MarkGood();
    EXPECT_EQ(0, env.writes);
}

TEST(BootControlTest, EnvFileMatchesTheModel)
{
    std::ifstream file(SATLINK_UBOOT_ENV_FILE);
    ASSERT_TRUE(file.good()) << SATLINK_UBOOT_ENV_FILE;
    std::map<std::string, std::string> vars;
    std::string line;
    while (std::getline(file, line))
    {
        const auto eq = line.find('=');
        if (!line.empty() && line[0] != '#' && eq != std::string::npos)
        {
            vars[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    EXPECT_EQ("a", vars["boot_slot"]);
    EXPECT_EQ("3", vars["bootlimit"]);
    EXPECT_EQ("0", vars["upgrade_available"]);
    EXPECT_NE(std::string::npos, vars["altbootcmd"].find("rollback"));
    EXPECT_NE(std::string::npos, vars["altbootcmd"].find("satlink_golden"));
    EXPECT_NE(std::string::npos, vars["satlink_args"].find("satlink.slot=${boot_slot}"));
}

int Cli(MemEnv &env, const std::string &cmdline, std::vector<std::string> args, bool healthy,
        std::string *text = nullptr)
{
    CliContext ctx{env, cmdline, [healthy] { return healthy; }};
    std::ostringstream out;
    std::ostringstream err;
    const int rc = RunBootctl(args, ctx, out, err);
    if (text != nullptr)
    {
        *text = out.str() + err.str();
    }
    return rc;
}

TEST(BootctlCliTest, StatusShowsPrediction)
{
    MemEnv env;
    env.vars["upgrade_available"] = "1";
    env.vars["bootcount"] = "3";
    std::string text;
    EXPECT_EQ(kExitOk, Cli(env, "satlink.slot=a", {"status"}, true, &text));
    EXPECT_NE(std::string::npos, text.find("state:      trial"));
    EXPECT_NE(std::string::npos, text.find("bootcount:  3/3"));
    EXPECT_NE(std::string::npos, text.find("next boot:  slot b"));
}

TEST(BootctlCliTest, ConfirmIsHealthGated)
{
    MemEnv env;
    EXPECT_EQ(kExitOk, Cli(env, "satlink.slot=a", {"confirm"}, false)); // nothing on trial
    EXPECT_EQ(kExitOk, Cli(env, "satlink.slot=a", {"prepare-update"}, true));
    SimulateReset(env);
    EXPECT_EQ(kExitUnhealthy, Cli(env, "satlink.slot=b", {"confirm"}, false));
    EXPECT_EQ("1", env.vars["upgrade_available"]);
    EXPECT_EQ(kExitOk, Cli(env, "satlink.slot=b", {"confirm"}, true));
    EXPECT_EQ("0", env.vars["upgrade_available"]);
}

TEST(BootctlCliTest, InactiveAndErrors)
{
    MemEnv env;
    std::string text;
    EXPECT_EQ(kExitOk, Cli(env, "satlink.slot=a", {"inactive"}, true, &text));
    EXPECT_EQ("/dev/mmcblk0p3\n", text);
    EXPECT_EQ(kExitUsage, Cli(env, "", {}, true));
    EXPECT_EQ(kExitUsage, Cli(env, "", {"frob"}, true));
    EXPECT_EQ(kExitError, Cli(env, "satlink.slot=golden", {"mark-good"}, true));
}

} // namespace
} // namespace satlink::bootctl
