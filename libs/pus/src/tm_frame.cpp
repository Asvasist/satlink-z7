/**
 * @file tm_frame.cpp
 * @implements SRS-PUS-002
 */
#include "satlink/pus/tm_frame.hpp"

#include <algorithm>

#include "satlink/pus/space_packet.hpp"

namespace satlink::pus {
namespace {
/// Largest packet the demultiplexer accepts (a corrupt length must not grow the buffer).
constexpr std::size_t kMaxPacket = 4096;
} // namespace

void EncodeFrameHeader(const FrameHeader &h, Frame &frame)
{
    const unsigned word0 = ((h.spacecraft_id & 0x3FFU) << 4U) | ((h.vc & 0x7U) << 1U);
    const unsigned status = (0x3U << 11U) | (h.first_header_pointer & 0x7FFU);
    frame[0] = static_cast<std::uint8_t>(word0 >> 8U);
    frame[1] = static_cast<std::uint8_t>(word0 & 0xFFU);
    frame[2] = h.mc_count;
    frame[3] = h.vc_count;
    frame[4] = static_cast<std::uint8_t>(status >> 8U);
    frame[5] = static_cast<std::uint8_t>(status & 0xFFU);
}

FrameHeader DecodeFrameHeader(const Frame &frame)
{
    FrameHeader h;
    const unsigned word0 = (static_cast<unsigned>(frame[0]) << 8U) | frame[1];
    const unsigned status = (static_cast<unsigned>(frame[4]) << 8U) | frame[5];
    h.spacecraft_id = static_cast<std::uint16_t>((word0 >> 4U) & 0x3FFU);
    h.vc = static_cast<std::uint8_t>((word0 >> 1U) & 0x7U);
    h.mc_count = frame[2];
    h.vc_count = frame[3];
    h.first_header_pointer = static_cast<std::uint16_t>(status & 0x7FFU);
    return h;
}

bool FrameMultiplexer::Enqueue(std::uint8_t vc, std::vector<std::uint8_t> packet)
{
    if (vc >= kIdleVc || packet.size() < kPrimaryHeaderSize ||
        channels_[vc].queue.size() >= queue_limit_)
    {
        ++dropped_;
        return false;
    }
    channels_[vc].queue.push_back(std::move(packet));
    return true;
}

bool FrameMultiplexer::HasData() const
{
    return std::any_of(channels_.begin(), channels_.end(),
                       [](const Channel &c) { return !c.queue.empty() || !c.partial.empty(); });
}

std::size_t FrameMultiplexer::Queued(std::uint8_t vc) const
{
    return vc < kVirtualChannels ? channels_[vc].queue.size() : 0U;
}

Frame FrameMultiplexer::NextFrame()
{
    Frame frame{};
    FrameHeader h;
    h.spacecraft_id = scid_;
    h.mc_count = mc_count_++;

    // A channel with a packet in progress first, then the highest-priority channel with data.
    int vc = -1;
    for (std::uint8_t i = 0; i < kIdleVc && vc < 0; ++i)
    {
        if (!channels_[i].partial.empty())
        {
            vc = i;
        }
    }
    for (std::uint8_t i = 0; i < kIdleVc && vc < 0; ++i)
    {
        if (!channels_[i].queue.empty())
        {
            vc = i;
        }
    }
    if (vc < 0)
    {
        Channel &idle = channels_[kIdleVc];
        h.vc = kIdleVc;
        h.vc_count = idle.count++;
        h.first_header_pointer = kFhpIdleOnly;
        EncodeFrameHeader(h, frame);
        std::fill(frame.begin() + kFrameHeaderSize, frame.end(), 0x55);
        return frame;
    }

    Channel &ch = channels_[static_cast<std::size_t>(vc)];
    h.vc = static_cast<std::uint8_t>(vc);
    h.vc_count = ch.count++;
    std::size_t pos = kFrameHeaderSize;
    std::uint16_t fhp = kFhpNoPacketStart;

    auto put = [&](std::vector<std::uint8_t> &bytes) {
        const std::size_t n = std::min(bytes.size(), kFrameSize - pos);
        std::copy_n(bytes.begin(), n, frame.begin() + static_cast<std::ptrdiff_t>(pos));
        pos += n;
        bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
    };

    if (!ch.partial.empty())
    {
        put(ch.partial);
    }
    while (pos < kFrameSize)
    {
        std::vector<std::uint8_t> next;
        if (!ch.queue.empty())
        {
            next = std::move(ch.queue.front());
            ch.queue.pop_front();
        }
        else
        {
            next = EncodeIdle(std::max<std::size_t>(kFrameSize - pos, kPrimaryHeaderSize + 1U));
        }
        if (fhp == kFhpNoPacketStart)
        {
            fhp = static_cast<std::uint16_t>(pos - kFrameHeaderSize);
        }
        put(next);
        ch.partial = std::move(next); // empty unless it did not fit
    }
    h.first_header_pointer = fhp;
    EncodeFrameHeader(h, frame);
    return frame;
}

void FrameDemultiplexer::Extract(std::uint8_t vc, Channel &ch)
{
    while (ch.buffer.size() >= kPrimaryHeaderSize)
    {
        const auto h = DecodePrimaryHeader(ch.buffer);
        if (!h)
        {
            return;
        }
        const std::size_t size = PacketSize(*h);
        if (size > kMaxPacket || (ch.buffer[0] >> 5U) != 0U)
        {
            ch.buffer.clear();
            ch.synced = false;
            ++stats_.resyncs;
            return;
        }
        if (ch.buffer.size() < size)
        {
            return;
        }
        if (h->apid == kIdleApid)
        {
            ++stats_.idle_packets;
        }
        else
        {
            ++stats_.packets;
            handler_(vc, std::span<const std::uint8_t>(ch.buffer.data(), size));
        }
        ch.buffer.erase(ch.buffer.begin(), ch.buffer.begin() + static_cast<std::ptrdiff_t>(size));
    }
}

void FrameDemultiplexer::Push(const Frame &frame)
{
    ++stats_.frames;
    const FrameHeader h = DecodeFrameHeader(frame);
    if (h.spacecraft_id != scid_ || (frame[0] >> 6U) != 0U)
    {
        ++stats_.wrong_spacecraft;
        return;
    }
    if (h.first_header_pointer == kFhpIdleOnly)
    {
        ++stats_.idle_frames;
        return;
    }
    Channel &ch = channels_[h.vc];
    if (ch.have_count && h.vc_count != ch.expected)
    {
        stats_.lost_frames += static_cast<std::uint8_t>(h.vc_count - ch.expected);
        if (!ch.buffer.empty())
        {
            ++stats_.resyncs;
        }
        ch.buffer.clear();
        ch.synced = false;
    }
    ch.have_count = true;
    ch.expected = static_cast<std::uint8_t>(h.vc_count + 1U);

    const auto *data = frame.data() + kFrameHeaderSize;
    const std::uint16_t fhp = h.first_header_pointer;
    if (fhp != kFhpNoPacketStart && fhp >= kFrameDataSize)
    {
        ch.buffer.clear(); // corrupt pointer: wait for the next frame
        ch.synced = false;
        return;
    }
    if (!ch.synced)
    {
        if (fhp == kFhpNoPacketStart)
        {
            return;
        }
        ch.buffer.assign(data + fhp, data + kFrameDataSize);
        ch.synced = true;
    }
    else
    {
        if (fhp != kFhpNoPacketStart && ch.buffer.size() >= kPrimaryHeaderSize)
        {
            // The packet in progress must end exactly where the pointer says a new one starts.
            const auto partial = DecodePrimaryHeader(ch.buffer);
            const std::size_t missing =
                partial ? PacketSize(*partial) - ch.buffer.size() : kFrameDataSize;
            if (missing != fhp)
            {
                ++stats_.resyncs;
                ch.buffer.assign(data + fhp, data + kFrameDataSize);
                Extract(h.vc, ch);
                return;
            }
        }
        ch.buffer.insert(ch.buffer.end(), data, data + kFrameDataSize);
    }
    Extract(h.vc, ch);
}

} // namespace satlink::pus
