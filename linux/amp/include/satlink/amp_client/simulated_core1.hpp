/**
 * @file simulated_core1.hpp
 * @brief The Core 1 firmware's modem application running in-process behind a MessagePort,
 *        with the software loopback as the "RF" link.
 *
 * Used where there is no board: host tests, the payload simulator (satlink-payloadd --sim) and
 * the hardware-in-the-loop test suite's simulated target. Time is simulated: each Advance()
 * produces the symbols that would have been sent in that time at 6 kSym/s, so a test can run
 * minutes of link time in seconds, or Advance() can be driven from a real clock.
 *
 * @implements SRS-AMP-007
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "satlink/amp_client/message_port.hpp"
#include "satlink/modem_app/modem_app.h"

namespace satlink::amp {

class SimulatedCore1 final : public MessagePort
{
  public:
    SimulatedCore1();
    ~SimulatedCore1() override;

    SimulatedCore1(const SimulatedCore1 &) = delete;
    SimulatedCore1 &operator=(const SimulatedCore1 &) = delete;
    SimulatedCore1(SimulatedCore1 &&) = delete;
    SimulatedCore1 &operator=(SimulatedCore1 &&) = delete;

    int Send(std::uint16_t type, std::span<const std::uint8_t> payload) override;
    /// Does not advance time; returns -ETIMEDOUT at once when nothing is queued.
    int Receive(Message &msg, std::chrono::milliseconds timeout) override;

    /// Run @p ms of link time (messages from Linux are processed first).
    void Advance(std::uint32_t ms);

    [[nodiscard]] std::uint32_t NowMs() const;
    /// Direct access for tests (for example the receiver statistics).
    [[nodiscard]] const satlink_modem_app_t &App() const
    {
        return *app_;
    }

  private:
    static satlink_status_t SendMsg(void *ctx, std::uint16_t type, const std::uint8_t *payload,
                                    std::uint16_t len);

    mutable std::mutex mutex_;
    std::unique_ptr<satlink_modem_app_t> app_;
    std::deque<Message> to_linux_;
    std::deque<Message> to_rtos_;
    std::uint32_t now_ms_ = 0;
};

} // namespace satlink::amp
