/**
 * @file hkc_link.cpp
 * @implements SRS-HKC-005
 */
#include "satlink/hal/hkc_link.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>

#include "satlink/boot/can_boot.h"
#include "satlink/can/can_frame.h"
#include "satlink/hk/telemetry.h"

namespace satlink::hal {
namespace {

using std::chrono::milliseconds;

constexpr milliseconds kWaitSlice{100};
/// Frames that are not the ones waited for, tolerated before giving up on a wait.
constexpr unsigned kMaxUnrelatedFrames = 10000;
/// Frames received without the upload advancing, after which the sender retransmits.
constexpr unsigned kFloodLimit = 1000;
constexpr unsigned kEnterAttempts = 3;
constexpr std::size_t kResponseLength = 8;

satlink_can_frame_t ToC(const CanFrame &frame)
{
    satlink_can_frame_t out{};
    out.id = frame.id;
    out.dlc = std::min<std::uint8_t>(frame.dlc, static_cast<std::uint8_t>(SATLINK_CAN_MAX_DLC));
    out.extended = false;
    out.rtr = false;
    std::copy_n(frame.data.begin(), out.dlc, std::begin(out.data));
    return out;
}

CanFrame FromC(const satlink_can_frame_t &frame)
{
    CanFrame out;
    out.id = frame.id;
    out.dlc = std::min<std::uint8_t>(frame.dlc, static_cast<std::uint8_t>(SATLINK_CAN_MAX_DLC));
    std::copy_n(std::begin(frame.data), out.dlc, out.data.begin());
    return out;
}

/// Waits for a frame @p accept likes, counting a Receive() without a frame as its full timeout.
template <typename Accept>
std::optional<CanFrame> WaitFor(CanPort &port, milliseconds timeout, Accept accept)
{
    milliseconds waited{0};
    unsigned unrelated = 0;
    while (waited < timeout && unrelated < kMaxUnrelatedFrames)
    {
        const milliseconds slice = std::min(kWaitSlice, timeout - waited);
        std::optional<CanFrame> frame = port.Receive(slice);
        if (!frame)
        {
            waited += slice;
        }
        else if (accept(*frame))
        {
            return frame;
        }
        else
        {
            ++unrelated;
        }
    }
    return std::nullopt;
}

bool IsResponse(const CanFrame &frame, satlink_canboot_rsp_t kind)
{
    return frame.id == SATLINK_CANBOOT_ID_RSP && frame.dlc == kResponseLength &&
           frame.data[0] == static_cast<std::uint8_t>(kind);
}

std::uint16_t Le16(const CanFrame &frame, std::size_t at)
{
    return static_cast<std::uint16_t>(frame.data[at] | (frame.data[at + 1] << 8));
}

std::uint32_t Le32(const CanFrame &frame, std::size_t at)
{
    return static_cast<std::uint32_t>(Le16(frame, at)) |
           (static_cast<std::uint32_t>(Le16(frame, at + 2)) << 16);
}

HkcReceiverState ToReceiverState(std::uint8_t value)
{
    switch (value)
    {
    case 0:
        return HkcReceiverState::kIdle;
    case 1:
        return HkcReceiverState::kReceiving;
    case 2:
        return HkcReceiverState::kVerified;
    default:
        return HkcReceiverState::kError;
    }
}

HkcLevel ToLevel(satlink_hk_level_t level)
{
    switch (level)
    {
    case SATLINK_HK_OK:
        return HkcLevel::kOk;
    case SATLINK_HK_WARN:
        return HkcLevel::kWarn;
    default:
        return HkcLevel::kAlarm;
    }
}

std::optional<HkcNodeInfo> PingOnce(CanPort &port, milliseconds timeout)
{
    CanFrame ping;
    ping.id = SATLINK_CANBOOT_ID_CMD;
    ping.dlc = 1;
    ping.data[0] = static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_PING);
    port.Send(ping);

    const std::optional<CanFrame> reply = WaitFor(port, timeout, [](const CanFrame &frame) {
        return IsResponse(frame, SATLINK_CANBOOT_RSP_PONG);
    });
    if (!reply)
    {
        return std::nullopt;
    }

    HkcNodeInfo info;
    info.state = ToReceiverState(reply->data[1]);
    info.protocol_version = Le16(*reply, 2);
    info.max_image_size = Le32(*reply, 4);
    return info;
}

} // namespace

std::string DescribeCanBootError(std::uint16_t code)
{
    switch (code)
    {
    case SATLINK_CANBOOT_ERR_OPCODE:
        return "unknown command";
    case SATLINK_CANBOOT_ERR_STATE:
        return "command not allowed in the node's current state";
    case SATLINK_CANBOOT_ERR_SIZE:
        return "image empty or larger than the node accepts";
    case SATLINK_CANBOOT_ERR_SEQ:
        return "gap in the frame sequence";
    case SATLINK_CANBOOT_ERR_LENGTH:
        return "frame with the wrong length";
    case SATLINK_CANBOOT_ERR_WRITE:
        return "the node could not store the data";
    case SATLINK_CANBOOT_ERR_CRC:
        return "image CRC mismatch";
    case SATLINK_CANBOOT_ERR_INCOMPLETE:
        return "the node did not receive the whole image";
    case SATLINK_CANBOOT_ERR_TIMEOUT:
        return "no answer from the node";
    default:
        return "unknown error";
    }
}

HkcLink::HkcLink(CanPort &port, HkcLinkOptions options) : port_(port), options_(options) {}

std::optional<HkcNodeInfo> HkcLink::Ping()
{
    return PingOnce(port_, options_.response_timeout);
}

std::optional<HkcNodeInfo> HkcLink::EnterBootloader()
{
    if (const std::optional<HkcNodeInfo> info = Ping())
    {
        return info;
    }

    satlink_can_frame_t enter{};
    satlink_canboot_make_enter(&enter);
    const CanFrame enter_frame = FromC(enter);

    bool acknowledged = false;
    for (unsigned attempt = 0; attempt < kEnterAttempts && !acknowledged; ++attempt)
    {
        port_.Send(enter_frame);
        acknowledged =
            WaitFor(port_, options_.response_timeout, [](const CanFrame &frame) {
                return IsResponse(frame, SATLINK_CANBOOT_RSP_ACK) &&
                       frame.data[1] == static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_ENTER);
            }).has_value();
    }
    if (!acknowledged)
    {
        return std::nullopt;
    }

    const milliseconds interval = std::max(options_.ping_interval, milliseconds{1});
    for (milliseconds waited{0}; waited < options_.bootloader_wait; waited += interval)
    {
        if (const std::optional<HkcNodeInfo> info = PingOnce(port_, interval))
        {
            return info;
        }
    }
    return std::nullopt;
}

HkcUploadResult HkcLink::Upload(std::span<const std::uint8_t> image, bool boot_after,
                                const ProgressFn &progress)
{
    HkcUploadResult result;
    satlink_canboot_tx_t tx{};
    const auto window = static_cast<std::uint16_t>(std::min<unsigned>(options_.window, 0xFFFFU));
    const auto retries = static_cast<std::uint8_t>(std::min<unsigned>(options_.max_retries, 255U));

    if (image.size() > std::numeric_limits<std::uint32_t>::max() ||
        satlink_canboot_tx_init(&tx, image.data(), static_cast<std::uint32_t>(image.size()), window,
                                boot_after, retries) != SATLINK_OK)
    {
        result.message = "the image is empty or larger than " +
                         std::to_string(SATLINK_CANBOOT_MAX_IMAGE) + " bytes";
        return result;
    }

    unsigned reported = std::numeric_limits<unsigned>::max();
    std::uint16_t seen_acked = tx.acked_seq;
    satlink_canboot_tx_state_t seen_state = tx.state;
    unsigned frames_without_progress = 0;

    for (;;)
    {
        satlink_can_frame_t out{};
        while (satlink_canboot_tx_next(&tx, &out))
        {
            port_.Send(FromC(out));
        }

        const unsigned percent = satlink_canboot_tx_progress(&tx);
        if (progress && percent != reported)
        {
            reported = percent;
            progress(percent);
        }

        const satlink_canboot_tx_state_t state = satlink_canboot_tx_state(&tx);
        if (state == SATLINK_CANBOOT_TX_DONE)
        {
            result.ok = true;
            result.message = boot_after ? "image verified and started" : "image verified";
            return result;
        }
        if (state == SATLINK_CANBOOT_TX_FAILED)
        {
            result.node_error = satlink_canboot_tx_error(&tx);
            result.message =
                (result.node_error == SATLINK_CANBOOT_ERR_TIMEOUT)
                    ? "the node stopped answering; is its bootloader running?"
                    : "the node refused the image: " + DescribeCanBootError(result.node_error);
            return result;
        }

        const std::optional<CanFrame> in = port_.Receive(options_.response_timeout);
        if (!in)
        {
            satlink_canboot_tx_on_timeout(&tx);
            frames_without_progress = 0;
            continue;
        }

        const satlink_can_frame_t received = ToC(*in);
        (void)satlink_canboot_tx_on_response(&tx, &received);
        if (tx.acked_seq != seen_acked || tx.state != seen_state)
        {
            seen_acked = tx.acked_seq;
            seen_state = tx.state;
            frames_without_progress = 0;
        }
        else if (++frames_without_progress >= kFloodLimit)
        {
            // Frames keep arriving but none moves the transfer on: do not wait for a quiet bus.
            satlink_canboot_tx_on_timeout(&tx);
            frames_without_progress = 0;
        }
    }
}

std::optional<HkcTelemetry> HkcLink::ReadTelemetry(std::chrono::milliseconds timeout)
{
    satlink_hk_snapshot_t snapshot{};
    std::optional<std::uint8_t> sequence;
    unsigned received_mask = 0;
    constexpr unsigned kAllFrames = (1U << SATLINK_HK_FRAME_COUNT) - 1U;

    const std::optional<CanFrame> done = WaitFor(port_, timeout, [&](const CanFrame &frame) {
        if (frame.id < SATLINK_HK_ID_THERMAL ||
            frame.id >= (SATLINK_HK_ID_THERMAL + SATLINK_HK_FRAME_COUNT))
        {
            return false;
        }

        const satlink_can_frame_t incoming = ToC(frame);
        satlink_hk_snapshot_t probe{};
        std::uint8_t seq = 0;
        if (satlink_hk_decode(&incoming, &probe, &seq) != SATLINK_OK)
        {
            return false;
        }
        if (!sequence || *sequence != seq)
        {
            // The first frame of a newer snapshot: drop what was collected of the older one.
            sequence = seq;
            snapshot = satlink_hk_snapshot_t{};
            received_mask = 0;
        }
        (void)satlink_hk_decode(&incoming, &snapshot, &seq);
        received_mask |= 1U << (frame.id - SATLINK_HK_ID_THERMAL);
        return received_mask == kAllFrames;
    });
    if (!done)
    {
        return std::nullopt;
    }

    HkcTelemetry telemetry;
    telemetry.temperature_mdegc = snapshot.temp_mdegc;
    telemetry.vccint_mv = snapshot.vccint_mv;
    telemetry.vccaux_mv = snapshot.vccaux_mv;
    telemetry.vccbram_mv = snapshot.vccbram_mv;
    telemetry.temperature_level = ToLevel(snapshot.temp_level);
    telemetry.vccint_level = ToLevel(snapshot.vccint_level);
    telemetry.vccaux_level = ToLevel(snapshot.vccaux_level);
    telemetry.vccbram_level = ToLevel(snapshot.vccbram_level);
    telemetry.uptime_s = snapshot.uptime_s;
    telemetry.watchdog_reset = snapshot.watchdog_reset;
    return telemetry;
}

} // namespace satlink::hal
