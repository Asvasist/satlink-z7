/**
 * @file tm_frame.hpp
 * @brief CCSDS TM Transfer Frames (132.0-B-3) carrying Space Packets over virtual channels.
 *
 * One transfer frame is exactly one modem frame payload (128 bytes): a 6-byte primary header
 * and a 122-byte data field. No operational control field and no frame error control field:
 * the physical layer already protects every frame with a CRC-16 (frames with a bad CRC never
 * reach this layer).
 *
 * Primary header: version (2 bits, 0) | spacecraft ID (10) | virtual channel ID (3) | OCF flag
 * (1, 0) | master channel frame count (8) | virtual channel frame count (8) | data field status
 * (16: secondary header 0, sync 0, packet order 0, segment length ID 11, first header pointer
 * 11 bits).
 *
 * Packets are packed back to back and may continue in the next frame of the same virtual
 * channel. The first header pointer (FHP) gives the offset of the first packet that starts in
 * the frame (0x7FF: none starts here, 0x7FE: idle data only). The demultiplexer uses the frame
 * counters to detect lost frames and the FHP to find the next packet boundary after a loss.
 *
 * @implements SRS-PUS-002
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

namespace satlink::pus {

inline constexpr std::size_t kFrameSize = 128;
inline constexpr std::size_t kFrameHeaderSize = 6;
inline constexpr std::size_t kFrameDataSize = kFrameSize - kFrameHeaderSize;
inline constexpr std::uint16_t kFhpNoPacketStart = 0x7FF;
inline constexpr std::uint16_t kFhpIdleOnly = 0x7FE;
inline constexpr std::uint8_t kVirtualChannels = 8;
inline constexpr std::uint8_t kIdleVc = 7;

using Frame = std::array<std::uint8_t, kFrameSize>;

struct FrameHeader
{
    std::uint16_t spacecraft_id = 0;
    std::uint8_t vc = 0;
    std::uint8_t mc_count = 0;
    std::uint8_t vc_count = 0;
    std::uint16_t first_header_pointer = kFhpNoPacketStart;
};

void EncodeFrameHeader(const FrameHeader &h, Frame &frame);
FrameHeader DecodeFrameHeader(const Frame &frame);

/**
 * @brief Packs queued packets into frames.
 *
 * Channels are served in priority order (VC 0 first). A channel that is in the middle of a
 * packet keeps priority until the packet is finished, so a frame never interleaves two
 * channels' packets. The unused tail of a frame is filled with an idle packet, which may itself
 * continue in the next frame of that channel. With nothing to send, NextFrame() returns an
 * idle frame on VC 7.
 */
class FrameMultiplexer
{
  public:
    explicit FrameMultiplexer(std::uint16_t spacecraft_id, std::size_t queue_limit = 64)
        : scid_(spacecraft_id), queue_limit_(queue_limit)
    {}

    /// Queue a complete packet. False (and counted) when the channel's queue is full.
    bool Enqueue(std::uint8_t vc, std::vector<std::uint8_t> packet);

    [[nodiscard]] bool HasData() const;
    [[nodiscard]] std::size_t Queued(std::uint8_t vc) const;
    [[nodiscard]] std::uint32_t Dropped() const
    {
        return dropped_;
    }

    Frame NextFrame();

  private:
    struct Channel
    {
        std::deque<std::vector<std::uint8_t>> queue;
        std::vector<std::uint8_t> partial; ///< rest of a packet that did not fit
        std::uint8_t count = 0;
    };

    std::uint16_t scid_;
    std::size_t queue_limit_;
    std::array<Channel, kVirtualChannels> channels_{};
    std::uint8_t mc_count_ = 0;
    std::uint32_t dropped_ = 0;
};

/// Counters of the demultiplexer.
struct DemuxStats
{
    std::uint32_t frames = 0;
    std::uint32_t idle_frames = 0;
    std::uint32_t wrong_spacecraft = 0;
    std::uint32_t lost_frames = 0; ///< from gaps in the VC frame counters
    std::uint32_t resyncs = 0;     ///< partial packets thrown away
    std::uint32_t packets = 0;
    std::uint32_t idle_packets = 0;
};

/**
 * @brief Extracts packets from received frames.
 */
class FrameDemultiplexer
{
  public:
    using PacketHandler = std::function<void(std::uint8_t vc, std::span<const std::uint8_t>)>;

    FrameDemultiplexer(std::uint16_t spacecraft_id, PacketHandler handler)
        : scid_(spacecraft_id), handler_(std::move(handler))
    {}

    void Push(const Frame &frame);

    [[nodiscard]] const DemuxStats &Stats() const
    {
        return stats_;
    }

  private:
    struct Channel
    {
        std::vector<std::uint8_t> buffer;
        bool synced = false;
        bool have_count = false;
        std::uint8_t expected = 0;
    };

    void Extract(std::uint8_t vc, Channel &ch);

    std::uint16_t scid_;
    PacketHandler handler_;
    std::array<Channel, kVirtualChannels> channels_{};
    DemuxStats stats_;
};

} // namespace satlink::pus
