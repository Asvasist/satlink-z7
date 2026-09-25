/**
 * @file link_sim.hpp
 * @brief Test helper: transmitter -> RRC -> channel -> matched filter -> receiver.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "satlink/modem/channel.h"
#include "satlink/modem/frame.h"
#include "satlink/modem/receiver.h"
#include "satlink/modem/rrc.h"

namespace satlink::test {

class LinkSim
{
  public:
    explicit LinkSim(std::uint32_t seed = 1)
    {
        satlink_fir_init_rrc(&tx_fir_);
        satlink_fir_init_rrc(&rx_fir_);
        satlink_channel_init(&channel, seed);
        satlink_rx_init(&rx, &OnFrame, this);
    }

    /// Sends one frame through the link. @p info may be empty for IDLE frames.
    void Send(std::uint8_t modcod, satlink_frame_type_t type, const std::vector<std::uint8_t> &info)
    {
        satlink_frame_header_t h{modcod, static_cast<std::uint8_t>(type),
                                 static_cast<std::uint8_t>(seq_++ & 0x7FU)};
        std::vector<satlink_cf_t> sym(SATLINK_FRAME_MAX_SYMBOLS);
        std::size_t n = 0;
        satlink_frame_build(&h, info.empty() ? nullptr : info.data(), sym.data(), sym.size(), &n);
        sym.resize(n);
        SendSymbols(sym);
    }

    /// Random symbols that are not a frame (preamble noise, loss of signal, ...).
    void SendFiller(std::size_t count)
    {
        std::vector<satlink_cf_t> sym(count);
        std::uint32_t x = 0x1234567U;
        for (auto &s : sym)
        {
            x = x * 1664525U + 1013904223U;
            s.re = (x & 0x80000000U) ? 0.7071F : -0.7071F;
            s.im = (x & 0x40000000U) ? 0.7071F : -0.7071F;
        }
        SendSymbols(sym);
    }

    void SendSymbols(const std::vector<satlink_cf_t> &sym)
    {
        std::vector<satlink_cf_t> samples(sym.size() * SATLINK_MODEM_SPS);
        satlink_rrc_interpolate(&tx_fir_, sym.data(), sym.size(), samples.data());
        satlink_channel_apply(&channel, samples.data(), samples.size());
        for (auto &s : samples)
        {
            s = satlink_fir_push(&rx_fir_, s);
        }
        // Feed in odd-sized chunks, as DMA blocks would arrive.
        std::size_t off = 0;
        while (off < samples.size())
        {
            const std::size_t len = std::min<std::size_t>(333, samples.size() - off);
            satlink_rx_push(&rx, samples.data() + off, len);
            off += len;
        }
    }

    /// Flush the filter delays with a few idle frames' worth of silence.
    void Flush()
    {
        SendFiller(64);
    }

    satlink_channel_t channel{};
    satlink_receiver_t rx{};
    std::vector<satlink_rx_frame_t> frames;

  private:
    static void OnFrame(void *ctx, const satlink_rx_frame_t *frame)
    {
        static_cast<LinkSim *>(ctx)->frames.push_back(*frame);
    }

    satlink_fir_t tx_fir_{};
    satlink_fir_t rx_fir_{};
    unsigned seq_ = 0;
};

} // namespace satlink::test
