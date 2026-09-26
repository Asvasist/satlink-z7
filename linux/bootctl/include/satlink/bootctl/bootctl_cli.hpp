/**
 * @file bootctl_cli.hpp
 * @brief Command logic of satlink-bootctl.
 *
 * @implements SRS-BOOT-002
 */
#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include "satlink/bootctl/boot_control.hpp"

namespace satlink::bootctl {

inline constexpr int kExitOk = 0;
inline constexpr int kExitError = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitUnhealthy = 4;

struct CliContext
{
    EnvStore &env;
    std::string cmdline;
    /// True when the system reached a healthy state (systemd "running").
    std::function<bool()> healthy;
};

int RunBootctl(const std::vector<std::string> &args, CliContext &ctx, std::ostream &out,
               std::ostream &err);

} // namespace satlink::bootctl
