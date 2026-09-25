/**
 * @file test_space_packet.cpp
 * @brief Space packets with PUS-C headers: exact header bytes, round trips, every rejection.
 *
 * @verifies SRS-PUS-001
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/pus/space_packet.hpp"

namespace satlink::pus {
namespace {

TEST(SpacePacketTest, TelecommandWireFormat)
{
    Telecommand tc;
    tc.apid = 0x010;
    tc.sequence_count = 5;
    tc.ack_flags = kAckAcceptance | kAckCompletion;
    tc.service = 17;
    tc.subtype = 1;
    tc.source_id = 0x1234;
    const auto p = Encode(tc);
    ASSERT_EQ(6U + 5U + 2U, p.size());
    // 0001 1 000 0001 0000: version 0, TC, secondary header, APID 0x010
    EXPECT_EQ(0x18, p[0]);
    EXPECT_EQ(0x10, p[1]);
    EXPECT_EQ(0xC0, p[2]); // unsegmented, count 5
    EXPECT_EQ(0x05, p[3]);
    EXPECT_EQ(0x00, p[4]);
    EXPECT_EQ(0x06, p[5]); // 7 data bytes - 1
    EXPECT_EQ(0x29, p[6]); // PUS version 2, ack 1001
    EXPECT_EQ(17, p[7]);
    EXPECT_EQ(1, p[8]);
    EXPECT_EQ(0x12, p[9]);
    EXPECT_EQ(0x34, p[10]);

    Telecommand out;
    ASSERT_EQ(DecodeError::kNone, Decode(p, out));
    EXPECT_EQ(0x010, out.apid);
    EXPECT_EQ(5, out.sequence_count);
    EXPECT_EQ(17, out.service);
    EXPECT_EQ(0x1234, out.source_id);
    EXPECT_TRUE(out.data.empty());
}

TEST(SpacePacketTest, TelemetryRoundTripWithTimeAndData)
{
    Telemetry tm;
    tm.apid = 0x010;
    tm.sequence_count = 0x3FFF;
    tm.service = 3;
    tm.subtype = 25;
    tm.message_counter = 42;
    tm.destination_id = 7;
    tm.time = CucTime::FromUnixMs(1760000000500ULL);
    tm.data = {1, 2, 3, 4, 5};
    const auto p = Encode(tm);
    EXPECT_EQ(6U + 13U + 5U + 2U, p.size());
    EXPECT_EQ(0x08, p[0]); // TM, secondary header
    Telemetry out;
    ASSERT_EQ(DecodeError::kNone, Decode(p, out));
    EXPECT_EQ(0x3FFF, out.sequence_count);
    EXPECT_EQ(25, out.subtype);
    EXPECT_EQ(42, out.message_counter);
    EXPECT_EQ(7, out.destination_id);
    EXPECT_EQ(tm.data, out.data);
    EXPECT_EQ(1760000000500ULL, out.time.ToUnixMs());
    EXPECT_EQ(1760000000U - 946684800U, out.time.seconds);
    EXPECT_EQ(32768, out.time.fraction);
}

TEST(SpacePacketTest, Rejections)
{
    Telecommand tc;
    tc.service = 17;
    tc.subtype = 1;
    auto p = Encode(tc);
    Telecommand out;
    Telemetry tm_out;

    EXPECT_EQ(DecodeError::kWrongType, Decode(p, tm_out));
    auto bad = p;
    bad.back() ^= 1;
    EXPECT_EQ(DecodeError::kCrc, Decode(bad, out));
    bad = p;
    bad.push_back(0);
    EXPECT_EQ(DecodeError::kLengthMismatch, Decode(bad, out));
    bad = p;
    bad[0] |= 0x20; // packet version 1
    EXPECT_EQ(DecodeError::kBadVersion, Decode(bad, out));
    bad = p;
    bad[6] = static_cast<std::uint8_t>((1U << 4U) | 0x9U); // PUS version 1
    EXPECT_EQ(DecodeError::kBadPusVersion, Decode(bad, out));
    EXPECT_EQ(DecodeError::kTooShort, Decode(std::vector<std::uint8_t>{1, 2, 3}, out));
    const std::vector<std::uint8_t> payload{9, 9};
    const auto raw = EncodeRaw(PacketType::kTelecommand, 0x3F1, 1, payload);
    EXPECT_EQ(DecodeError::kNoSecondaryHeader, Decode(raw, out));
    EXPECT_STREQ("CRC error", ToString(DecodeError::kCrc));
}

TEST(SpacePacketTest, RawAndIdlePackets)
{
    const std::vector<std::uint8_t> ip(100, 0xAB);
    const auto raw = EncodeRaw(PacketType::kTelemetry, 0x3F0, 9, ip);
    ASSERT_EQ(106U, raw.size());
    const auto h = DecodePrimaryHeader(raw);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(0x3F0, h.value_or(PrimaryHeader{}).apid);
    EXPECT_FALSE(h.value_or(PrimaryHeader{}).secondary_header);
    EXPECT_EQ(106U, PacketSize(h.value_or(PrimaryHeader{})));

    const auto idle = EncodeIdle(40);
    EXPECT_EQ(40U, idle.size());
    EXPECT_EQ(kIdleApid, DecodePrimaryHeader(idle).value_or(PrimaryHeader{}).apid);
    EXPECT_EQ(7U, EncodeIdle(2).size()); // minimum: header + 1 byte
}

TEST(SpacePacketTest, SequenceCounterWrapsAt14Bits)
{
    SequenceCounter c;
    for (int i = 0; i < 0x3FFF; ++i)
    {
        c.Next();
    }
    EXPECT_EQ(0x3FFF, c.Next());
    EXPECT_EQ(0, c.Next());
}

} // namespace
} // namespace satlink::pus
