/**
 * @file fake_hkc_node.hpp
 * @brief A CAN bus with a housekeeping controller on it, for the host tests of the Linux side.
 *
 * In bootloader mode the node is the real receiver of satlink/boot/can_boot.h storing into a
 * memory buffer, so the Linux code is tested against the code that runs on the MicroBlaze V.
 * The bus can lose and duplicate frames and corrupt one DATA frame. In application mode the node
 * only understands ENTER, like the real application, and then needs a few frames before its
 * bootloader answers.
 *
 * Time is virtual: a Receive() that finds nothing adds its timeout to waited().
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <vector>

#include "satlink/boot/can_boot.h"
#include "satlink/can/can_frame.h"
#include "satlink/hal/can_port.hpp"
#include "satlink/hk/telemetry.h"

namespace satlink::hal::test {

inline satlink_can_frame_t ToC(const CanFrame &frame)
{
    satlink_can_frame_t out{};
    out.id = frame.id;
    out.dlc = frame.dlc;
    std::copy_n(frame.data.begin(), frame.dlc, std::begin(out.data));
    return out;
}

inline CanFrame FromC(const satlink_can_frame_t &frame)
{
    CanFrame out;
    out.id = frame.id;
    out.dlc = frame.dlc;
    std::copy_n(std::begin(frame.data), frame.dlc, out.data.begin());
    return out;
}

class FakeHkcNode final : public CanPort
{
  public:
    enum class Mode
    {
        kBootloader,  ///< the bootloader is running and answers
        kApplication, ///< the application is running: it only understands ENTER
        kStarting,    ///< the bootloader has just started and is not answering yet
        kDead,        ///< nothing answers
    };

    explicit FakeHkcNode(Mode mode = Mode::kBootloader, std::uint32_t max_image = 4096U)
        : mode_(mode), memory_(max_image, 0xFFU)
    {
        const satlink_canboot_rx_config_t config = {
            .write = &FakeHkcNode::Store, .ctx = this, .max_size = max_image, .window = 0U};
        satlink_canboot_rx_init(&receiver_, &config);
    }

    FakeHkcNode(const FakeHkcNode &) = delete;
    FakeHkcNode &operator=(const FakeHkcNode &) = delete;

    void Send(const CanFrame &frame) override
    {
        sent.push_back(frame);
        for (unsigned copy = 0; copy < (duplicate ? 2U : 1U); ++copy)
        {
            if (Drops())
            {
                continue;
            }
            switch (mode_)
            {
            case Mode::kApplication:
                OnApplicationFrame(frame);
                break;
            case Mode::kStarting:
                if (frames_until_ready > 0)
                {
                    --frames_until_ready;
                }
                if (frames_until_ready == 0)
                {
                    mode_ = Mode::kBootloader;
                }
                break;
            case Mode::kBootloader:
                OnBootloaderFrame(frame);
                break;
            case Mode::kDead:
                break;
            }
        }
    }

    std::optional<CanFrame> Receive(std::chrono::milliseconds timeout) override
    {
        if (inbox_.empty())
        {
            waited += timeout;
            return std::nullopt;
        }
        CanFrame frame = inbox_.front();
        inbox_.pop_front();
        return frame;
    }

    /// Puts frames on the bus as if the application had broadcast them.
    void Broadcast(const CanFrame &frame)
    {
        inbox_.push_back(frame);
    }

    /// Puts the frames selected by @p mask (bit n = frame n) of one snapshot on the bus.
    void BroadcastTelemetry(const satlink_hk_snapshot_t &snapshot, std::uint8_t seq,
                            unsigned mask = 7U)
    {
        satlink_can_frame_t frames[SATLINK_HK_FRAME_COUNT];
        satlink_hk_pack(&snapshot, seq, frames);
        for (unsigned i = 0; i < SATLINK_HK_FRAME_COUNT; ++i)
        {
            if ((mask & (1U << i)) != 0U)
            {
                inbox_.push_back(FromC(frames[i]));
            }
        }
    }

    Mode mode() const
    {
        return mode_;
    }

    const satlink_canboot_rx_t &receiver() const
    {
        return receiver_;
    }

    /// The bytes received so far, up to the size announced by BEGIN.
    std::vector<std::uint8_t> Received() const
    {
        const std::uint32_t size = satlink_canboot_rx_image_size(&receiver_);
        return {memory_.begin(), memory_.begin() + static_cast<std::ptrdiff_t>(size)};
    }

    const std::vector<std::uint8_t> &memory() const
    {
        return memory_;
    }

    std::vector<CanFrame> sent;
    std::chrono::milliseconds waited{0};
    unsigned drop_percent = 0;
    bool duplicate = false;
    unsigned corrupt_data_frame = 0; ///< corrupt the n-th DATA frame that arrives; 0 = none
    unsigned frames_until_ready = 4; ///< frames the bootloader ignores after ENTER

  private:
    static satlink_status_t Store(void *ctx, std::uint32_t offset, const std::uint8_t *data,
                                  std::size_t len)
    {
        auto &memory = static_cast<FakeHkcNode *>(ctx)->memory_;
        if (static_cast<std::size_t>(offset) + len > memory.size())
        {
            return SATLINK_ERR_RANGE;
        }
        std::copy_n(data, len, memory.begin() + static_cast<std::ptrdiff_t>(offset));
        return SATLINK_OK;
    }

    bool Drops()
    {
        seed_ = (seed_ * 1664525U) + 1013904223U;
        return ((seed_ >> 16U) % 100U) < drop_percent;
    }

    void OnApplicationFrame(const CanFrame &frame)
    {
        if (frame.id == SATLINK_CANBOOT_ID_CMD && frame.dlc >= 1 &&
            frame.data[0] == static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_ENTER))
        {
            CanFrame ack;
            ack.id = SATLINK_CANBOOT_ID_RSP;
            ack.dlc = 8;
            ack.data[0] = static_cast<std::uint8_t>(SATLINK_CANBOOT_RSP_ACK);
            ack.data[1] = static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_ENTER);
            if (!Drops())
            {
                inbox_.push_back(ack);
            }
            mode_ = Mode::kStarting;
        }
    }

    void OnBootloaderFrame(CanFrame frame)
    {
        if (frame.id == SATLINK_CANBOOT_ID_CMD && frame.dlc > 3 &&
            frame.data[0] == static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_DATA))
        {
            ++data_frames_;
            if (data_frames_ == corrupt_data_frame)
            {
                frame.data[3] = static_cast<std::uint8_t>(frame.data[3] ^ 0x01U);
            }
        }

        const satlink_can_frame_t incoming = ToC(frame);
        satlink_can_frame_t response{};
        bool have_response = false;
        satlink_canboot_rx_handle(&receiver_, &incoming, &response, &have_response);
        if (have_response && !Drops())
        {
            inbox_.push_back(FromC(response));
        }
    }

    Mode mode_;
    std::vector<std::uint8_t> memory_;
    satlink_canboot_rx_t receiver_{};
    std::deque<CanFrame> inbox_;
    std::uint32_t seed_ = 12345U;
    unsigned data_frames_ = 0;
};

} // namespace satlink::hal::test
