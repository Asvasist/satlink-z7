/**
 * @file simulated_core1.cpp
 * @implements SRS-AMP-007
 */
#include "satlink/amp_client/simulated_core1.hpp"

#include <cerrno>

namespace satlink::amp {
namespace {
constexpr std::uint32_t kSymbolsPerMs = 6; // 6 kSym/s
constexpr std::uint32_t kStepMs = 10;      // like the firmware's 10 ms loop
} // namespace

SimulatedCore1::SimulatedCore1(ModcodSelector selector)
    : app_(std::make_unique<satlink_modem_app_t>()), selector_(std::move(selector))
{
    satlink_modem_app_hw_t hw{};
    hw.ctx = this;
    hw.send_msg = &SendMsg;
    hw.select_modcod = selector_ ? &SelectModcod : nullptr;
    satlink_modem_app_init(app_.get(), &hw);
    app_->config.loopback = SATLINK_LOOP_SOFTWARE;
}

SimulatedCore1::~SimulatedCore1() = default;

satlink_status_t SimulatedCore1::SendMsg(void *ctx, std::uint16_t type, const std::uint8_t *payload,
                                         std::uint16_t len)
{
    auto *self = static_cast<SimulatedCore1 *>(ctx);
    // Called with mutex_ held (from Advance()).
    self->to_linux_.push_back({type, std::vector<std::uint8_t>(payload, payload + len)});
    return SATLINK_OK;
}

std::uint8_t SimulatedCore1::SelectModcod(void *ctx, float esn0_db, bool locked,
                                          std::uint8_t current)
{
    return static_cast<SimulatedCore1 *>(ctx)->selector_(esn0_db, locked, current);
}

int SimulatedCore1::Send(std::uint16_t type, std::span<const std::uint8_t> payload)
{
    if (payload.size() > SATLINK_MSG_MAX_BYTES)
    {
        return -EINVAL;
    }
    std::lock_guard lock(mutex_);
    to_rtos_.push_back({type, {payload.begin(), payload.end()}});
    return 0;
}

int SimulatedCore1::Receive(Message &msg, std::chrono::milliseconds)
{
    std::lock_guard lock(mutex_);
    if (to_linux_.empty())
    {
        return -ETIMEDOUT;
    }
    msg = std::move(to_linux_.front());
    to_linux_.pop_front();
    return 0;
}

void SimulatedCore1::Advance(std::uint32_t ms)
{
    std::lock_guard lock(mutex_);
    std::uint32_t left = ms;
    while (left > 0)
    {
        while (!to_rtos_.empty())
        {
            const Message m = std::move(to_rtos_.front());
            to_rtos_.pop_front();
            satlink_modem_app_on_msg(app_.get(), m.type, m.payload.data(),
                                     static_cast<std::uint16_t>(m.payload.size()));
        }
        const std::uint32_t step = left < kStepMs ? left : kStepMs;
        if (app_->config.loopback != SATLINK_LOOP_SOFTWARE)
        {
            app_->config.loopback = SATLINK_LOOP_SOFTWARE; // nothing else exists here
        }
        satlink_modem_app_run_loopback(app_.get(), static_cast<std::size_t>(step) * kSymbolsPerMs);
        now_ms_ += step;
        satlink_modem_app_tick(app_.get(), now_ms_);
        left -= step;
    }
}

std::uint32_t SimulatedCore1::NowMs() const
{
    std::lock_guard lock(mutex_);
    return now_ms_;
}

} // namespace satlink::amp
