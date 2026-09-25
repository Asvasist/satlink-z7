/**
 * @file space_packet.hpp
 * @brief CCSDS Space Packets (133.0-B-2) with PUS-C secondary headers (ECSS-E-ST-70-41C) and the
 *        packet error control field (CRC-16-CCITT).
 *
 * Telecommand (TC) data field header, 5 bytes: PUS version (4 bits, = 2) | acknowledgement flags
 * (4) | service type (8) | subtype (8) | source ID (16).
 *
 * Telemetry (TM) data field header, 13 bytes: PUS version (4, = 2) | time reference status (4) |
 * service type (8) | subtype (8) | message type counter (16) | destination ID (16) | time: CUC
 * with 4 bytes of seconds and 2 bytes of 2^-16 s since the mission epoch 2000-01-01T00:00:00Z.
 *
 * All multi-byte fields big-endian. Every PUS packet ends with a CRC-16-CCITT over the whole
 * packet before it.
 *
 * @implements SRS-PUS-001
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace satlink::pus {

inline constexpr std::size_t kPrimaryHeaderSize = 6;
inline constexpr std::size_t kTcSecondaryHeaderSize = 5;
inline constexpr std::size_t kTmSecondaryHeaderSize = 13;
inline constexpr std::size_t kCrcSize = 2;
inline constexpr std::uint16_t kIdleApid = 0x7FF;
inline constexpr std::uint8_t kPusVersion = 2;
/// Unix time of the mission epoch 2000-01-01T00:00:00Z.
inline constexpr std::uint64_t kMissionEpochUnix = 946684800ULL;

/// Acknowledgement flags of a TC (which ST[01] reports the sender wants).
enum AckFlags : std::uint8_t
{
    kAckAcceptance = 0x1,
    kAckStart = 0x2,
    kAckProgress = 0x4,
    kAckCompletion = 0x8,
};

enum class PacketType : std::uint8_t
{
    kTelemetry = 0,
    kTelecommand = 1,
};

struct PrimaryHeader
{
    PacketType type = PacketType::kTelemetry;
    bool secondary_header = true;
    std::uint16_t apid = 0;           ///< 11 bits
    std::uint8_t sequence_flags = 3;  ///< 3 = unsegmented
    std::uint16_t sequence_count = 0; ///< 14 bits
    std::uint16_t data_length = 0;    ///< packet data field length - 1
};

/// CUC time, 4 + 2 bytes.
struct CucTime
{
    std::uint32_t seconds = 0;
    std::uint16_t fraction = 0; ///< 1/65536 s

    static CucTime FromUnixMs(std::uint64_t unix_ms);
    [[nodiscard]] std::uint64_t ToUnixMs() const;
};

struct Telecommand
{
    std::uint16_t apid = 0;
    std::uint16_t sequence_count = 0;
    std::uint8_t ack_flags = kAckAcceptance | kAckCompletion;
    std::uint8_t service = 0;
    std::uint8_t subtype = 0;
    std::uint16_t source_id = 0;
    std::vector<std::uint8_t> data;
};

struct Telemetry
{
    std::uint16_t apid = 0;
    std::uint16_t sequence_count = 0;
    std::uint8_t service = 0;
    std::uint8_t subtype = 0;
    std::uint16_t message_counter = 0;
    std::uint16_t destination_id = 0;
    CucTime time;
    std::vector<std::uint8_t> data;
};

/// Why a packet was rejected.
enum class DecodeError
{
    kNone,
    kTooShort,
    kBadVersion,     ///< Packet version number != 0
    kLengthMismatch, ///< data_length does not match the buffer
    kWrongType,      ///< TC expected, TM found (or the reverse)
    kNoSecondaryHeader,
    kBadPusVersion,
    kCrc,
};

const char *ToString(DecodeError error);

void EncodePrimaryHeader(const PrimaryHeader &h, std::span<std::uint8_t, kPrimaryHeaderSize> out);
std::optional<PrimaryHeader> DecodePrimaryHeader(std::span<const std::uint8_t> in);

/// Total packet size announced by a primary header (6 + data_length + 1).
std::size_t PacketSize(const PrimaryHeader &h);

std::vector<std::uint8_t> Encode(const Telecommand &tc);
std::vector<std::uint8_t> Encode(const Telemetry &tm);

/// Decodes and checks a complete packet (length, versions, CRC).
DecodeError Decode(std::span<const std::uint8_t> packet, Telecommand &tc);
DecodeError Decode(std::span<const std::uint8_t> packet, Telemetry &tm);

/// A packet without secondary header or CRC (used for IP datagrams and idle packets).
std::vector<std::uint8_t> EncodeRaw(PacketType type, std::uint16_t apid,
                                    std::uint16_t sequence_count,
                                    std::span<const std::uint8_t> data);

/// Idle packet of exactly @p total_size bytes (>= 7), APID 0x7FF.
std::vector<std::uint8_t> EncodeIdle(std::size_t total_size);

/// 14-bit sequence counter per APID.
class SequenceCounter
{
  public:
    std::uint16_t Next()
    {
        const std::uint16_t value = next_;
        next_ = static_cast<std::uint16_t>((next_ + 1U) & 0x3FFFU);
        return value;
    }

  private:
    std::uint16_t next_ = 0;
};

} // namespace satlink::pus
