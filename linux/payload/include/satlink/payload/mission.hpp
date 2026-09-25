/**
 * @file mission.hpp
 * @brief Mission-specific PUS definitions of SatLink-Z7: APIDs, virtual channels, functions,
 *        housekeeping structures and events. The ground station and the test suite use the
 *        same numbers (docs/icd section 5).
 *
 * @implements SRS-PUS-003
 */
#pragma once

#include <cstdint>

namespace satlink::payload {

inline constexpr std::uint16_t kSpacecraftId = 0x2A7;
inline constexpr std::uint16_t kApidPayload = 0x010; ///< PUS services of the payload manager
inline constexpr std::uint16_t kApidIpDown = 0x3F0;  ///< IP datagrams, satellite -> ground
inline constexpr std::uint16_t kApidIpUp = 0x3F1;    ///< IP datagrams, ground -> satellite
inline constexpr std::uint16_t kGroundId = 0x0001;   ///< PUS source/destination ID of the GS

inline constexpr std::uint8_t kVcTelemetry = 0;
inline constexpr std::uint8_t kVcIp = 1;

inline constexpr std::uint16_t kTcPort = 10025; ///< UDP: TC to the payload
inline constexpr std::uint16_t kTmPort = 10026; ///< UDP: TM to the ground station

/// ST[08] function IDs (TC[8,1] data: function ID, then arguments, big-endian).
enum class Function : std::uint8_t
{
    kSetModcod = 1,  ///< u8 MODCOD (turns ACM off)
    kSetAcm = 2,     ///< u8 enable, u8 min, u8 max, s16 margin 0.01 dB, s16 hysteresis 0.01 dB
    kSetChannel = 3, ///< u16 noise level, u16 gain Q15 (stops a running pass)
    kStartPass = 4,  ///< u16 max elevation 0.01 deg, s16 zenith Es/N0 0.01 dB, u8 time scale
    kStopPass = 5,
    kSetLoopback = 6,  ///< u8 0 analog, 1 digital, 2 software
    kRestartModem = 7, ///< restart the Core 1 firmware
};

/// ST[03] structure IDs.
enum class HkStructure : std::uint8_t
{
    kModem = 1,
    kLink = 2,
    kPlatform = 3,
};

/// ST[05] event IDs.
enum class Event : std::uint16_t
{
    kLinkLocked = 1,
    kLinkLost = 2,
    kModcodChanged = 3,
    kAos = 4,
    kLos = 5,
    kFirmwareLog = 6,
    kModemRestarted = 7,
};

/// ST[01] failure codes (data of TM[1,2] / TM[1,8] after the TC's packet ID and sequence).
enum class FailureCode : std::uint16_t
{
    kUnknownService = 1,
    kUnknownSubtype = 2,
    kBadData = 3,
    kUnknownFunction = 4,
    kModemError = 5,
    kBadApid = 6,
    kCorruptPacket = 7,
};

} // namespace satlink::payload
