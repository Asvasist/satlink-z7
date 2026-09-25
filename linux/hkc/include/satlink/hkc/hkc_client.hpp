/**
 * @file hkc_client.hpp
 * @brief Linux side of the housekeeping controller protocols: CAN bootloader client, command
 *        client and housekeeping frame decoding.
 *
 * @implements SRS-HKC-006
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "satlink/hal/can_bus.hpp"
#include "satlink/hkc/hk_proto.h"

namespace satlink::hkc {

/// The bootloader or application answered with an error status.
class HkcError : public std::runtime_error
{
  public:
    HkcError(const std::string &what, std::uint8_t status)
        : std::runtime_error(what), status_(status)
    {}
    [[nodiscard]] std::uint8_t Status() const
    {
        return status_;
    }

  private:
    std::uint8_t status_;
};

/// No answer within the retry budget.
class HkcTimeout : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct ClientOptions
{
    std::chrono::milliseconds response_timeout{200};
    int retries = 3; ///< Extra attempts per request after a timeout.
};

/// Answer to a bootloader PING.
struct BootloaderInfo
{
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;
    bool app_valid = false;
    std::uint8_t state = 0;
};

/**
 * @brief Talks to the HKC bootloader and application over one CanBus.
 *
 * Every request waits for its own response; unrelated frames that arrive in between (periodic
 * housekeeping from the application, for example) are skipped. A request that times out is
 * repeated, which the bootloader protocol makes safe (duplicate DATA frames are not written
 * twice).
 */
class HkcClient
{
  public:
    using Progress = std::function<void(std::size_t done, std::size_t total)>;

    explicit HkcClient(hal::CanBus &bus, ClientOptions options = {}) : bus_(bus), options_(options)
    {}

    BootloaderInfo Ping();

    /// Checks @p image locally (header, CRCs, location), then downloads and verifies it.
    /// Throws std::invalid_argument for an image that would be rejected anyway.
    void Flash(std::span<const std::uint8_t> image, const Progress &progress = {});

    /// Starts the application that the bootloader holds.
    void Boot();

    /// Cancels a download in progress.
    void Abort();

    /// Sends an application command and returns its acknowledgement (any status).
    satlink_hk_ack_t Command(std::uint8_t opcode, std::span<const std::uint8_t> args = {});

    /// Sends a time sync frame (no acknowledgement).
    void SyncTime(std::chrono::system_clock::time_point now);

  private:
    satlink_can_frame_t Transact(const satlink_can_frame_t &request,
                                 const std::function<bool(const satlink_can_frame_t &)> &match);
    satlink_can_frame_t BootRequest(std::uint8_t opcode, std::span<const std::uint8_t> payload);

    hal::CanBus &bus_;
    ClientOptions options_;
};

/// One line describing a housekeeping or protocol frame, or nullopt for unknown identifiers.
std::optional<std::string> DescribeFrame(const satlink_can_frame_t &frame);

/// Text for a bootloader status byte.
const char *CanbootStatusName(std::uint8_t status);

} // namespace satlink::hkc
