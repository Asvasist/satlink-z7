/**
 * @file test_tm_frame.cpp
 * @brief TM transfer frames: header layout, packets spanning frames, virtual channel priority,
 *        idle frames, and recovery after lost frames.
 *
 * @verifies SRS-PUS-002
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "satlink/pus/space_packet.hpp"
#include "satlink/pus/tm_frame.hpp"

namespace satlink::pus {
namespace {

constexpr std::uint16_t kScid = 0x2A7;

std::vector<std::uint8_t> MakePacket(std::uint16_t apid, std::size_t data_len, std::uint8_t tag)
{
    std::vector<std::uint8_t> data(data_len);
    for (std::size_t i = 0; i < data_len; ++i)
    {
        data[i] = static_cast<std::uint8_t>(tag + i);
    }
    return EncodeRaw(PacketType::kTelemetry, apid, tag, data);
}

struct Received
{
    std::uint8_t vc;
    std::vector<std::uint8_t> packet;
};

class FrameTest : public ::testing::Test
{
  protected:
    FrameMultiplexer mux_{kScid};
    std::vector<Received> rx_;
    FrameDemultiplexer demux_{kScid, [this](std::uint8_t vc, std::span<const std::uint8_t> p) {
                                  rx_.push_back({vc, {p.begin(), p.end()}});
                              }};

    void Pump(int frames, int drop_index = -1)
    {
        for (int i = 0; i < frames; ++i)
        {
            const Frame f = mux_.NextFrame();
            if (i != drop_index)
            {
                demux_.Push(f);
            }
        }
    }
};

TEST_F(FrameTest, HeaderLayout)
{
    Frame f{};
    EncodeFrameHeader({kScid, 5, 0x12, 0x34, 0x2A}, f);
    EXPECT_EQ(0x2A, f[0]); // 00 1010100111 101 0 -> 0x2A 0x7A
    EXPECT_EQ(0x7A, f[1]);
    EXPECT_EQ(0x12, f[2]);
    EXPECT_EQ(0x34, f[3]);
    EXPECT_EQ(0x18, f[4]); // segment length id 11, FHP 0x02A
    EXPECT_EQ(0x2A, f[5]);
    const auto h = DecodeFrameHeader(f);
    EXPECT_EQ(kScid, h.spacecraft_id);
    EXPECT_EQ(5, h.vc);
    EXPECT_EQ(0x2A, h.first_header_pointer);
}

TEST_F(FrameTest, EmptyMultiplexerSendsIdleFrames)
{
    const Frame f = mux_.NextFrame();
    const auto h = DecodeFrameHeader(f);
    EXPECT_EQ(kIdleVc, h.vc);
    EXPECT_EQ(kFhpIdleOnly, h.first_header_pointer);
    demux_.Push(f);
    EXPECT_EQ(1U, demux_.Stats().idle_frames);
    EXPECT_TRUE(rx_.empty());
}

TEST_F(FrameTest, PacketsSpanFramesAndArriveIntact)
{
    std::vector<std::vector<std::uint8_t>> sent;
    std::mt19937 gen(3); // NOLINT(cert-msc32-c,cert-msc51-cpp): reproducible test data
    for (int i = 0; i < 40; ++i)
    {
        sent.push_back(MakePacket(0x10, 1 + gen() % 300, static_cast<std::uint8_t>(i)));
        ASSERT_TRUE(mux_.Enqueue(0, sent.back()));
    }
    while (mux_.HasData())
    {
        demux_.Push(mux_.NextFrame());
    }
    ASSERT_EQ(sent.size(), rx_.size());
    for (std::size_t i = 0; i < sent.size(); ++i)
    {
        EXPECT_EQ(sent[i], rx_[i].packet) << i;
        EXPECT_EQ(0, rx_[i].vc);
    }
    EXPECT_EQ(0U, demux_.Stats().lost_frames);
    EXPECT_EQ(0U, demux_.Stats().resyncs);
}

TEST_F(FrameTest, FirstHeaderPointerMarksContinuations)
{
    ASSERT_TRUE(mux_.Enqueue(0, MakePacket(0x10, 200, 1))); // 206 bytes: 122 + 84
    const Frame f1 = mux_.NextFrame();
    const Frame f2 = mux_.NextFrame();
    EXPECT_EQ(0, DecodeFrameHeader(f1).first_header_pointer);
    // Frame 2: 84 bytes of continuation, then the idle packet that fills the rest.
    EXPECT_EQ(84, DecodeFrameHeader(f2).first_header_pointer);
    EXPECT_EQ(1, DecodeFrameHeader(f2).vc_count);
}

TEST_F(FrameTest, LowerVirtualChannelHasPriorityButPacketsAreNotInterleaved)
{
    ASSERT_TRUE(mux_.Enqueue(1, MakePacket(0x3F0, 300, 1)));
    const Frame first = mux_.NextFrame(); // starts the VC 1 packet
    EXPECT_EQ(1, DecodeFrameHeader(first).vc);
    ASSERT_TRUE(mux_.Enqueue(0, MakePacket(0x10, 20, 2)));
    const Frame second = mux_.NextFrame(); // VC 1 must finish its packet first
    EXPECT_EQ(1, DecodeFrameHeader(second).vc);
    demux_.Push(first);
    demux_.Push(second);
    while (mux_.HasData())
    {
        demux_.Push(mux_.NextFrame());
    }
    ASSERT_EQ(2U, rx_.size());
}

TEST_F(FrameTest, LostFrameCostsOnlyThePacketsItCarried)
{
    std::vector<std::vector<std::uint8_t>> sent;
    for (int i = 0; i < 30; ++i)
    {
        sent.push_back(MakePacket(0x10, 50, static_cast<std::uint8_t>(i))); // 56 bytes each
        ASSERT_TRUE(mux_.Enqueue(0, sent.back()));
    }
    // 30 * 56 = 1680 bytes: 14 frames. Drop frame 5.
    int n = 0;
    while (mux_.HasData())
    {
        const Frame f = mux_.NextFrame();
        if (n++ != 5)
        {
            demux_.Push(f);
        }
    }
    EXPECT_EQ(1U, demux_.Stats().lost_frames);
    // Frame 5 touched packets 10..13 (bytes 610..732); those plus the packet cut at the
    // resync are lost, everything else arrives unchanged and in order.
    EXPECT_GE(rx_.size(), 25U);
    EXPECT_LT(rx_.size(), 30U);
    for (const auto &r : rx_)
    {
        const std::uint8_t tag = r.packet[3]; // sequence count low byte = tag
        EXPECT_EQ(sent[tag], r.packet) << int{tag};
    }
    EXPECT_EQ(sent.back(), rx_.back().packet);
}

TEST_F(FrameTest, WrongSpacecraftAndCorruptPointerAreRejected)
{
    FrameMultiplexer other(0x001);
    ASSERT_TRUE(other.Enqueue(0, MakePacket(0x10, 10, 1)));
    demux_.Push(other.NextFrame());
    EXPECT_EQ(1U, demux_.Stats().wrong_spacecraft);

    ASSERT_TRUE(mux_.Enqueue(0, MakePacket(0x10, 10, 1)));
    Frame f = mux_.NextFrame();
    auto h = DecodeFrameHeader(f);
    h.first_header_pointer = 200; // beyond the data field
    EncodeFrameHeader(h, f);
    demux_.Push(f);
    EXPECT_TRUE(rx_.empty());
}

TEST_F(FrameTest, QueueLimitDropsAndCounts)
{
    FrameMultiplexer small(kScid, 2);
    EXPECT_TRUE(small.Enqueue(0, MakePacket(0x10, 5, 1)));
    EXPECT_TRUE(small.Enqueue(0, MakePacket(0x10, 5, 2)));
    EXPECT_FALSE(small.Enqueue(0, MakePacket(0x10, 5, 3)));
    EXPECT_FALSE(small.Enqueue(kIdleVc, MakePacket(0x10, 5, 4)));
    EXPECT_EQ(2U, small.Dropped());
    EXPECT_EQ(2U, small.Queued(0));
}

} // namespace
} // namespace satlink::pus
