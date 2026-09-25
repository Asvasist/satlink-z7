/**
 * @file amp_device.hpp
 * @brief MessagePort over /dev/satlink-amp (satlink_amp kernel driver). Linux targets only.
 *
 * @implements SRS-AMP-003
 */
#pragma once

#include <string>

#include "satlink/amp_client/message_port.hpp"

struct satlink_amp_status;

namespace satlink::amp {

class AmpDevice final : public MessagePort
{
  public:
    static constexpr const char *kPath = "/dev/satlink-amp";

    /// Throws std::system_error when the device cannot be opened.
    explicit AmpDevice(const std::string &path = kPath);
    ~AmpDevice() override;

    AmpDevice(const AmpDevice &) = delete;
    AmpDevice &operator=(const AmpDevice &) = delete;
    AmpDevice(AmpDevice &&) = delete;
    AmpDevice &operator=(AmpDevice &&) = delete;

    int Send(std::uint16_t type, std::span<const std::uint8_t> payload) override;
    int Receive(Message &msg, std::chrono::milliseconds timeout) override;

    int Start();
    int Stop();
    /// File descriptor for poll() (readable when a message is waiting).
    [[nodiscard]] int Fd() const
    {
        return fd_;
    }
    int GetStatus(satlink_amp_status &status);

  private:
    int fd_ = -1;
};

} // namespace satlink::amp
