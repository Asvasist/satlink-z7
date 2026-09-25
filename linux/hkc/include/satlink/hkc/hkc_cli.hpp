/**
 * @file hkc_cli.hpp
 * @brief Command logic of the satlink-hkc tool (housekeeping controller over CAN).
 *
 * @implements SRS-HKC-006
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "satlink/hal/can_bus.hpp"

namespace satlink::hkc {

inline constexpr const char *kDefaultInterface = "can0";

inline constexpr int kExitOk = 0;
inline constexpr int kExitRuntimeError = 1;
inline constexpr int kExitUsageError = 2;
inline constexpr int kExitTimeout = 3;

/// What the commands need from the environment; host tests supply fakes.
class Environment
{
  public:
    virtual ~Environment() = default;
    virtual std::unique_ptr<hal::CanBus> OpenCan(const std::string &interface) = 0;
    /// Whole file contents. Throws std::system_error when the file cannot be read.
    virtual std::vector<std::uint8_t> ReadFile(const std::string &path) = 0;
    virtual std::chrono::system_clock::time_point Now() = 0;
};

/// Runs one satlink-hkc command line (without the program name). Returns a kExit* code.
int RunHkc(const std::vector<std::string> &args, Environment &env, std::ostream &out,
           std::ostream &err);

} // namespace satlink::hkc
