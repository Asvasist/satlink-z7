/**
 * @file modem_client.hpp
 * @brief Typed Linux-side API to the Core 1 modem firmware over a MessagePort.
 *
 * @implements SRS-AMP-003
 */
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "satlink/amp/msg.h"
#include "satlink/amp_client/message_port.hpp"

namespace satlink::amp {

struct RxFrame
{
    std::uint8_t modcod = 0;
    bool crc_ok = false;
    float esn0_db = 0.0F;
    std::uint8_t seq = 0;
    std::array<std::uint8_t, SATLINK_MSG_FRAME_BYTES> data{};
};

struct LogLine
{
    std::uint8_t level = 0;
    std::string text;
};

/**
 * @brief Encodes commands and decodes firmware messages.
 *
 * Commands return 0 or a negative errno from the port. Poll() reads messages and hands them to
 * the callbacks; unknown or malformed messages are counted.
 */
class ModemClient
{
  public:
    explicit ModemClient(MessagePort &port) : port_(port) {}

    int Configure(const satlink_msg_modem_config_t &config);
    int SetChannel(std::uint16_t noise_level, std::uint16_t gain_q15);
    int SetAcm(const satlink_msg_acm_config_t &config);
    int SetTime(std::uint64_t unix_ms);
    int SendFrame(const std::array<std::uint8_t, SATLINK_MSG_FRAME_BYTES> &data);
    int Ping(std::uint32_t token);

    /// Read messages for up to @p timeout (returns after the first batch). Returns the number
    /// of messages handled, or a negative errno other than -ETIMEDOUT.
    int Poll(std::chrono::milliseconds timeout);

    /// Sends PING and polls until the matching PONG (other messages are dispatched as usual).
    /// Returns the firmware uptime, or nullopt on timeout.
    std::optional<std::uint32_t> PingAndWait(std::uint32_t token,
                                             std::chrono::milliseconds timeout);

    std::function<void(const RxFrame &)> on_rx_frame;
    std::function<void(const satlink_msg_status_t &)> on_status;
    std::function<void(const LogLine &)> on_log;
    std::function<void(const satlink_msg_ping_t &)> on_pong;
    std::function<void(const satlink_msg_constellation_t &)> on_constellation;

    [[nodiscard]] std::uint32_t BadMessages() const
    {
        return bad_messages_;
    }
    [[nodiscard]] const std::optional<satlink_msg_status_t> &LastStatus() const
    {
        return last_status_;
    }
    [[nodiscard]] const std::optional<satlink_msg_constellation_t> &LastConstellation() const
    {
        return last_constellation_;
    }

  private:
    void Dispatch(const Message &msg);

    MessagePort &port_;
    std::uint32_t bad_messages_ = 0;
    std::optional<satlink_msg_status_t> last_status_;
    std::optional<satlink_msg_constellation_t> last_constellation_;
};

} // namespace satlink::amp
