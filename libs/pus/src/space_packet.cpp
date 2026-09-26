/**
 * @file space_packet.cpp
 * @implements SRS-PUS-001
 */
#include "satlink/pus/space_packet.hpp"

#include "satlink/common/crc16_ccitt.h"

namespace satlink::pus {
namespace {

void Put16(std::vector<std::uint8_t> &v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 8U));
    v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
}

void Put32(std::vector<std::uint8_t> &v, std::uint32_t x)
{
    Put16(v, static_cast<std::uint16_t>(x >> 16U));
    Put16(v, static_cast<std::uint16_t>(x & 0xFFFFU));
}

std::uint16_t Get16(std::span<const std::uint8_t> p, std::size_t off)
{
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[off]) << 8U) | p[off + 1]);
}

std::uint32_t Get32(std::span<const std::uint8_t> p, std::size_t off)
{
    return (static_cast<std::uint32_t>(Get16(p, off)) << 16U) | Get16(p, off + 2);
}

void AppendCrc(std::vector<std::uint8_t> &v)
{
    Put16(v, satlink_crc16_ccitt(v.data(), v.size()));
}

std::vector<std::uint8_t> Finish(PacketType type, std::uint16_t apid, std::uint16_t seq,
                                 std::vector<std::uint8_t> body, bool crc)
{
    std::vector<std::uint8_t> packet(kPrimaryHeaderSize);
    PrimaryHeader h;
    h.type = type;
    h.secondary_header = crc; // PUS packets have both; raw packets neither
    h.apid = apid;
    h.sequence_count = seq;
    const std::size_t data_len = body.size() + (crc ? kCrcSize : 0U);
    h.data_length = static_cast<std::uint16_t>(data_len - 1U);
    EncodePrimaryHeader(
        h, std::span<std::uint8_t, kPrimaryHeaderSize>(packet.data(), kPrimaryHeaderSize));
    packet.insert(packet.end(), body.begin(), body.end());
    if (crc)
    {
        AppendCrc(packet);
    }
    return packet;
}

DecodeError CheckCommon(std::span<const std::uint8_t> packet, PacketType type,
                        std::size_t sec_header, PrimaryHeader &h)
{
    const auto header = DecodePrimaryHeader(packet);
    if (!header)
    {
        return DecodeError::kTooShort;
    }
    h = *header;
    if ((packet[0] >> 5U) != 0U)
    {
        return DecodeError::kBadVersion;
    }
    if (PacketSize(h) != packet.size())
    {
        return DecodeError::kLengthMismatch;
    }
    if (h.type != type)
    {
        return DecodeError::kWrongType;
    }
    if (!h.secondary_header)
    {
        return DecodeError::kNoSecondaryHeader;
    }
    if (packet.size() < kPrimaryHeaderSize + sec_header + kCrcSize)
    {
        return DecodeError::kTooShort;
    }
    if ((packet[kPrimaryHeaderSize] >> 4U) != kPusVersion)
    {
        return DecodeError::kBadPusVersion;
    }
    if (satlink_crc16_ccitt(packet.data(), packet.size()) != 0U)
    {
        return DecodeError::kCrc;
    }
    return DecodeError::kNone;
}

} // namespace

const char *ToString(DecodeError error)
{
    switch (error)
    {
    case DecodeError::kNone:
        return "ok";
    case DecodeError::kTooShort:
        return "too short";
    case DecodeError::kBadVersion:
        return "bad packet version";
    case DecodeError::kLengthMismatch:
        return "length mismatch";
    case DecodeError::kWrongType:
        return "wrong packet type";
    case DecodeError::kNoSecondaryHeader:
        return "no secondary header";
    case DecodeError::kBadPusVersion:
        return "bad PUS version";
    case DecodeError::kCrc:
        return "CRC error";
    }
    return "?";
}

CucTime CucTime::FromUnixMs(std::uint64_t unix_ms)
{
    CucTime t;
    const std::uint64_t epoch_ms = kMissionEpochUnix * 1000ULL;
    const std::uint64_t ms = unix_ms > epoch_ms ? unix_ms - epoch_ms : 0U;
    t.seconds = static_cast<std::uint32_t>(ms / 1000U);
    t.fraction = static_cast<std::uint16_t>(((ms % 1000U) * 65536U) / 1000U);
    return t;
}

std::uint64_t CucTime::ToUnixMs() const
{
    return ((kMissionEpochUnix + seconds) * 1000ULL) + ((fraction * 1000ULL + 32768U) / 65536ULL);
}

void EncodePrimaryHeader(const PrimaryHeader &h, std::span<std::uint8_t, kPrimaryHeaderSize> out)
{
    const unsigned word0 = (static_cast<unsigned>(h.type) << 12U) |
                           (h.secondary_header ? 0x0800U : 0U) | (h.apid & 0x7FFU);
    const unsigned word1 = ((h.sequence_flags & 0x3U) << 14U) | (h.sequence_count & 0x3FFFU);
    out[0] = static_cast<std::uint8_t>(word0 >> 8U);
    out[1] = static_cast<std::uint8_t>(word0 & 0xFFU);
    out[2] = static_cast<std::uint8_t>(word1 >> 8U);
    out[3] = static_cast<std::uint8_t>(word1 & 0xFFU);
    out[4] = static_cast<std::uint8_t>(h.data_length >> 8U);
    out[5] = static_cast<std::uint8_t>(h.data_length & 0xFFU);
}

std::optional<PrimaryHeader> DecodePrimaryHeader(std::span<const std::uint8_t> in)
{
    if (in.size() < kPrimaryHeaderSize)
    {
        return std::nullopt;
    }
    PrimaryHeader h;
    const std::uint16_t word0 = Get16(in, 0);
    const std::uint16_t word1 = Get16(in, 2);
    h.type = ((word0 >> 12U) & 1U) != 0U ? PacketType::kTelecommand : PacketType::kTelemetry;
    h.secondary_header = (word0 & 0x0800U) != 0U;
    h.apid = static_cast<std::uint16_t>(word0 & 0x7FFU);
    h.sequence_flags = static_cast<std::uint8_t>(word1 >> 14U);
    h.sequence_count = static_cast<std::uint16_t>(word1 & 0x3FFFU);
    h.data_length = Get16(in, 4);
    return h;
}

std::size_t PacketSize(const PrimaryHeader &h)
{
    return kPrimaryHeaderSize + static_cast<std::size_t>(h.data_length) + 1U;
}

std::vector<std::uint8_t> Encode(const Telecommand &tc)
{
    std::vector<std::uint8_t> body;
    body.push_back(static_cast<std::uint8_t>((kPusVersion << 4U) | (tc.ack_flags & 0x0FU)));
    body.push_back(tc.service);
    body.push_back(tc.subtype);
    Put16(body, tc.source_id);
    body.insert(body.end(), tc.data.begin(), tc.data.end());
    return Finish(PacketType::kTelecommand, tc.apid, tc.sequence_count, std::move(body), true);
}

std::vector<std::uint8_t> Encode(const Telemetry &tm)
{
    std::vector<std::uint8_t> body;
    body.push_back(static_cast<std::uint8_t>(kPusVersion << 4U));
    body.push_back(tm.service);
    body.push_back(tm.subtype);
    Put16(body, tm.message_counter);
    Put16(body, tm.destination_id);
    Put32(body, tm.time.seconds);
    Put16(body, tm.time.fraction);
    body.insert(body.end(), tm.data.begin(), tm.data.end());
    return Finish(PacketType::kTelemetry, tm.apid, tm.sequence_count, std::move(body), true);
}

DecodeError Decode(std::span<const std::uint8_t> packet, Telecommand &tc)
{
    PrimaryHeader h;
    const DecodeError err =
        CheckCommon(packet, PacketType::kTelecommand, kTcSecondaryHeaderSize, h);
    if (err != DecodeError::kNone)
    {
        return err;
    }
    const std::size_t s = kPrimaryHeaderSize;
    tc.apid = h.apid;
    tc.sequence_count = h.sequence_count;
    tc.ack_flags = static_cast<std::uint8_t>(packet[s] & 0x0FU);
    tc.service = packet[s + 1];
    tc.subtype = packet[s + 2];
    tc.source_id = Get16(packet, s + 3);
    tc.data.assign(packet.begin() + static_cast<std::ptrdiff_t>(s + kTcSecondaryHeaderSize),
                   packet.end() - static_cast<std::ptrdiff_t>(kCrcSize));
    return DecodeError::kNone;
}

DecodeError Decode(std::span<const std::uint8_t> packet, Telemetry &tm)
{
    PrimaryHeader h;
    const DecodeError err = CheckCommon(packet, PacketType::kTelemetry, kTmSecondaryHeaderSize, h);
    if (err != DecodeError::kNone)
    {
        return err;
    }
    const std::size_t s = kPrimaryHeaderSize;
    tm.apid = h.apid;
    tm.sequence_count = h.sequence_count;
    tm.service = packet[s + 1];
    tm.subtype = packet[s + 2];
    tm.message_counter = Get16(packet, s + 3);
    tm.destination_id = Get16(packet, s + 5);
    tm.time.seconds = Get32(packet, s + 7);
    tm.time.fraction = Get16(packet, s + 11);
    tm.data.assign(packet.begin() + static_cast<std::ptrdiff_t>(s + kTmSecondaryHeaderSize),
                   packet.end() - static_cast<std::ptrdiff_t>(kCrcSize));
    return DecodeError::kNone;
}

std::vector<std::uint8_t> EncodeRaw(PacketType type, std::uint16_t apid,
                                    std::uint16_t sequence_count,
                                    std::span<const std::uint8_t> data)
{
    return Finish(type, apid, sequence_count, {data.begin(), data.end()}, false);
}

std::vector<std::uint8_t> EncodeIdle(std::size_t total_size)
{
    const std::size_t n =
        total_size < kPrimaryHeaderSize + 1U ? kPrimaryHeaderSize + 1U : total_size;
    const std::vector<std::uint8_t> fill(n - kPrimaryHeaderSize, 0x55);
    return EncodeRaw(PacketType::kTelemetry, kIdleApid, 0, fill);
}

} // namespace satlink::pus
