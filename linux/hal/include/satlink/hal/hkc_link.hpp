/**
 * @file hkc_link.hpp
 * @brief Linux side of the housekeeping controller's CAN interface: restart it into its
 *        bootloader, upload a new application image, read its telemetry.
 *
 * The transfer logic is the same portable C code the bootloader runs (satlink/boot/can_boot.h);
 * this class only moves its frames over a CanPort and keeps the time.
 *
 * Time is counted in the waits: every Receive() that returns nothing is taken to have lasted as
 * long as its timeout, so a fake port that answers instantly does not make the tests slow.
 *
 * @implements SRS-HKC-005
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

#include "satlink/hal/can_port.hpp"

namespace satlink::hal {

struct HkcLinkOptions
{
    /// How long one request may go unanswered.
    std::chrono::milliseconds response_timeout{200};
    /// Unanswered requests in a row before an upload is given up.
    unsigned max_retries = 10;
    /// DATA frames in flight; 0 selects the protocol default.
    unsigned window = 0;
    /// After ENTER, how long to wait for the bootloader to come up. The bootloader starts a
    /// valid application again if no upload has begun about two seconds after it started.
    std::chrono::milliseconds bootloader_wait{3000};
    /// Time between PINGs while waiting for the bootloader.
    std::chrono::milliseconds ping_interval{50};
};

/// State of the bootloader's receiver, as reported in its PONG.
enum class HkcReceiverState : std::uint8_t
{
    kIdle = 0,
    kReceiving = 1,
    kVerified = 2,
    kError = 3,
};

struct HkcNodeInfo
{
    HkcReceiverState state = HkcReceiverState::kIdle;
    std::uint16_t protocol_version = 0;
    std::uint32_t max_image_size = 0;
};

struct HkcUploadResult
{
    bool ok = false;
    /// Reason reported by the node (SATLINK_CANBOOT_ERR_*), 0xFFFF if it did not answer, else 0.
    std::uint16_t node_error = 0;
    std::string message;
};

enum class HkcLevel : std::uint8_t
{
    kOk = 0,
    kWarn = 1,
    kAlarm = 2,
};

/// One housekeeping snapshot, from the three telemetry frames the application broadcasts.
struct HkcTelemetry
{
    std::int32_t temperature_mdegc = 0;
    std::uint16_t vccint_mv = 0;
    std::uint16_t vccaux_mv = 0;
    std::uint16_t vccbram_mv = 0;
    HkcLevel temperature_level = HkcLevel::kOk;
    HkcLevel vccint_level = HkcLevel::kOk;
    HkcLevel vccaux_level = HkcLevel::kOk;
    HkcLevel vccbram_level = HkcLevel::kOk;
    std::uint32_t uptime_s = 0;
    bool watchdog_reset = false;
};

class HkcLink
{
  public:
    /// Called with the percentage of the image acknowledged so far, each time it changes.
    using ProgressFn = std::function<void(unsigned percent)>;

    explicit HkcLink(CanPort &port, HkcLinkOptions options = HkcLinkOptions{});

    /// Asks the bootloader for its state. std::nullopt if nothing answers, which is also what a
    /// running application does: it only understands ENTER.
    std::optional<HkcNodeInfo> Ping();

    /// Makes sure the bootloader is running: pings it, and if it does not answer sends ENTER to
    /// the application and waits for the bootloader to come up. Returns its state, or
    /// std::nullopt if it never answered.
    std::optional<HkcNodeInfo> EnterBootloader();

    /// Uploads @p image to a bootloader that is already running (see EnterBootloader()), and
    /// starts it afterwards if @p boot_after is set.
    HkcUploadResult Upload(std::span<const std::uint8_t> image, bool boot_after,
                           const ProgressFn &progress = ProgressFn{});

    /// Waits up to @p timeout for one complete telemetry snapshot.
    std::optional<HkcTelemetry> ReadTelemetry(std::chrono::milliseconds timeout);

  private:
    CanPort &port_;
    HkcLinkOptions options_;
};

/// Text for a node error code from HkcUploadResult::node_error.
std::string DescribeCanBootError(std::uint16_t code);

} // namespace satlink::hal
