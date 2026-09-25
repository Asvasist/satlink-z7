/**
 * @file modem_client.cpp
 * @implements SRS-AMP-003
 */
#include "satlink/amp_client/modem_client.hpp"

#include <cerrno>
#include <span>

namespace satlink::amp {
namespace {

template <typename Encode, typename Msg>
int SendEncoded(MessagePort &port, std::uint16_t type, Encode encode, const Msg &msg)
{
    std::array<std::uint8_t, SATLINK_MSG_MAX_BYTES> buf{};
    const std::size_t n = encode(&msg, buf.data(), buf.size());
    if (n == 0)
    {
        return -EINVAL;
    }
    return port.Send(type, std::span(buf.data(), n));
}

} // namespace

int ModemClient::Configure(const satlink_msg_modem_config_t &config)
{
    return SendEncoded(port_, SATLINK_MSG_MODEM_CONFIG, &satlink_msg_encode_modem_config, config);
}

int ModemClient::SetChannel(std::uint16_t noise_level, std::uint16_t gain_q15)
{
    const satlink_msg_channel_t ch{noise_level, gain_q15};
    return SendEncoded(port_, SATLINK_MSG_CHANNEL, &satlink_msg_encode_channel, ch);
}

int ModemClient::SetAcm(const satlink_msg_acm_config_t &config)
{
    return SendEncoded(port_, SATLINK_MSG_ACM_CONFIG, &satlink_msg_encode_acm_config, config);
}

int ModemClient::SetTime(std::uint64_t unix_ms)
{
    const satlink_msg_time_t t{unix_ms};
    return SendEncoded(port_, SATLINK_MSG_TIME, &satlink_msg_encode_time, t);
}

int ModemClient::SendFrame(const std::array<std::uint8_t, SATLINK_MSG_FRAME_BYTES> &data)
{
    return port_.Send(SATLINK_MSG_TX_FRAME, data);
}

int ModemClient::Ping(std::uint32_t token)
{
    const satlink_msg_ping_t ping{token, 0};
    return SendEncoded(port_, SATLINK_MSG_PING, &satlink_msg_encode_ping, ping);
}

void ModemClient::Dispatch(const Message &msg)
{
    const auto *p = msg.payload.data();
    const auto n = msg.payload.size();
    switch (msg.type)
    {
    case SATLINK_MSG_RX_FRAME:
    {
        satlink_msg_rx_frame_t m{};
        if (satlink_msg_decode_rx_frame(p, n, &m) != SATLINK_OK)
        {
            break;
        }
        if (on_rx_frame)
        {
            RxFrame f;
            f.modcod = m.modcod;
            f.crc_ok = m.crc_ok;
            f.esn0_db = static_cast<float>(m.esn0_cdb) / 100.0F;
            f.seq = m.seq;
            std::copy(std::begin(m.data), std::end(m.data), f.data.begin());
            on_rx_frame(f);
        }
        return;
    }
    case SATLINK_MSG_STATUS:
    {
        satlink_msg_status_t s{};
        if (satlink_msg_decode_status(p, n, &s) != SATLINK_OK)
        {
            break;
        }
        last_status_ = s;
        if (on_status)
        {
            on_status(s);
        }
        return;
    }
    case SATLINK_MSG_LOG:
    {
        satlink_msg_log_t l{};
        if (satlink_msg_decode_log(p, n, &l) != SATLINK_OK)
        {
            break;
        }
        if (on_log)
        {
            on_log({l.level, l.text});
        }
        return;
    }
    case SATLINK_MSG_PONG:
    {
        satlink_msg_ping_t pong{};
        if (satlink_msg_decode_ping(p, n, &pong) != SATLINK_OK)
        {
            break;
        }
        if (on_pong)
        {
            on_pong(pong);
        }
        return;
    }
    default:
        break;
    }
    ++bad_messages_;
}

int ModemClient::Poll(std::chrono::milliseconds timeout)
{
    int handled = 0;
    Message msg;
    auto wait = timeout;
    for (;;)
    {
        const int rc = port_.Receive(msg, wait);
        if (rc == -ETIMEDOUT)
        {
            return handled;
        }
        if (rc != 0)
        {
            return rc;
        }
        Dispatch(msg);
        ++handled;
        wait = std::chrono::milliseconds{0}; // drain what is already queued, then return
    }
}

std::optional<std::uint32_t> ModemClient::PingAndWait(std::uint32_t token,
                                                      std::chrono::milliseconds timeout)
{
    std::optional<std::uint32_t> uptime;
    auto previous = on_pong;
    on_pong = [&](const satlink_msg_ping_t &pong) {
        if (pong.token == token)
        {
            uptime = pong.uptime_ms;
        }
        else if (previous)
        {
            previous(pong);
        }
    };
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    if (Ping(token) == 0)
    {
        while (!uptime && std::chrono::steady_clock::now() < deadline)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (Poll(std::max(left, std::chrono::milliseconds{1})) < 0)
            {
                break;
            }
        }
    }
    on_pong = previous;
    return uptime;
}

} // namespace satlink::amp
