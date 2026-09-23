/**
 * @file diag.hpp
 * @brief Command logic of the satlink-diag tool.
 *
 * @implements SRS-DIAG-001
 */
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "satlink/diag/device_factory.hpp"

namespace satlink::diag {

inline constexpr const char *kFrameAccelPath = "/dev/satlink-ccsds-frame-accel";
inline constexpr const char *kSpecTapPath = "/dev/satlink-spec-tap";
inline constexpr const char *kCodecBusPath = "/dev/i2c-0";

/// Process exit codes.
inline constexpr int kExitOk = 0;
inline constexpr int kExitRuntimeError = 1;
inline constexpr int kExitUsageError = 2;
inline constexpr int kExitTimeout = 3;

/**
 * @brief Runs one satlink-diag command line.
 *
 * @param args     The command line without the program name, for example {"fa", "version"}.
 * @param factory  Opens the devices the command touches.
 * @param out      Normal output.
 * @param err      Error and usage output.
 * @return One of the kExit* codes.
 */
int RunDiag(const std::vector<std::string> &args, DeviceFactory &factory, std::ostream &out,
            std::ostream &err);

} // namespace satlink::diag
