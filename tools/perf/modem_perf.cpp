/**
 * @file modem_perf.cpp
 * @brief Link performance sweep of the software modem: frame and bit error rate against Es/N0
 *        for every MODCOD, through the complete physical layer (framing, convolutional code,
 *        mapping, RRC, channel with carrier and timing offsets, AGC, timing and carrier
 *        recovery, frame sync, Viterbi).
 *
 *   satlink-modem-perf [--frames N] [--step DB] [--freq CPS] [--delay SAMPLES] [--modcod N]
 *       > perf.csv
 *
 * CSV columns: modcod, esn0_db, frames, frame_errors, missed, fer, bits, bit_errors, ber,
 * esn0_est_db. A frame counts as an error if it was not delivered with a good CRC and the
 * original data; BER is over the frames that were delivered (CRC good or not), so it shows the
 * decoder, while missed frames (no sync, bad header) only count in the FER.
 *
 * @implements SRS-PERF-001
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "satlink/modem/channel.h"
#include "satlink/modem/frame.h"
#include "satlink/modem/modcod.h"
#include "satlink/modem/receiver.h"
#include "satlink/modem/rrc.h"

namespace {

struct Options
{
    unsigned frames = 300;
    double step_db = 0.5;
    double freq_cps = 2.0e-4; ///< residual carrier offset, cycles per sample
    double delay = 0.37;      ///< fractional timing offset, samples
    int modcod = -1;          ///< one MODCOD only (-1: all)
};

struct Point
{
    unsigned frames = 0;
    unsigned frame_errors = 0;
    unsigned missed = 0;
    std::uint64_t bits = 0;
    std::uint64_t bit_errors = 0;
    double esn0_sum = 0.0;
    unsigned esn0_count = 0;
};

class Link
{
  public:
    Link(std::uint32_t seed, double esn0_db, const Options &o)
    {
        satlink_fir_init_rrc(&tx_fir_);
        satlink_fir_init_rrc(&rx_fir_);
        satlink_channel_init(&channel_, seed);
        satlink_channel_set_esn0(&channel_, static_cast<float>(esn0_db));
        channel_.freq_cps = static_cast<float>(o.freq_cps);
        channel_.delay = static_cast<float>(o.delay);
        channel_.phase_rad = 1.0F;
        satlink_rx_init(&rx_, &OnFrame, this);
    }

    void Send(std::uint8_t modcod, satlink_frame_type_t type, const std::uint8_t *info)
    {
        const satlink_frame_header_t h{modcod, static_cast<std::uint8_t>(type),
                                       static_cast<std::uint8_t>(seq_++ & 0x7FU)};
        std::vector<satlink_cf_t> sym(SATLINK_FRAME_MAX_SYMBOLS);
        std::size_t n = 0;
        satlink_frame_build(&h, info, sym.data(), sym.size(), &n);
        sym.resize(n);
        std::vector<satlink_cf_t> samples(n * SATLINK_MODEM_SPS);
        satlink_rrc_interpolate(&tx_fir_, sym.data(), n, samples.data());
        satlink_channel_apply(&channel_, samples.data(), samples.size());
        for (auto &s : samples)
        {
            s = satlink_fir_push(&rx_fir_, s);
        }
        satlink_rx_push(&rx_, samples.data(), samples.size());
    }

    std::vector<satlink_rx_frame_t> received;

  private:
    static void OnFrame(void *ctx, const satlink_rx_frame_t *f)
    {
        static_cast<Link *>(ctx)->received.push_back(*f);
    }

    satlink_fir_t tx_fir_{};
    satlink_fir_t rx_fir_{};
    satlink_channel_t channel_{};
    satlink_receiver_t rx_{};
    unsigned seq_ = 0;
};

unsigned Popcount(std::uint8_t x)
{
    unsigned n = 0;
    for (; x != 0U; x &= static_cast<std::uint8_t>(x - 1U))
    {
        ++n;
    }
    return n;
}

Point Measure(std::uint8_t modcod, double esn0_db, const Options &o)
{
    Link link(0x9E3779B9U ^ (static_cast<std::uint32_t>(esn0_db * 1000.0) + modcod * 7919U),
              esn0_db, o);
    // Acquisition on a few idle frames at the lowest MODCOD, as after AOS.
    for (int i = 0; i < 4; ++i)
    {
        link.Send(0, SATLINK_FRAME_IDLE, nullptr);
    }
    link.received.clear();

    struct Sent
    {
        unsigned index;
        std::vector<std::uint8_t> data;
    };
    std::map<std::uint8_t, Sent> sent;
    std::uint32_t rng = 0x12345678U ^ modcod;
    Point p;
    std::vector<std::uint8_t> info(SATLINK_FRAME_INFO_BYTES);
    auto evaluate = [&](const std::vector<satlink_rx_frame_t> &frames) {
        for (const auto &r : frames)
        {
            const auto it = sent.find(r.header.seq);
            if (r.header.type != SATLINK_FRAME_DATA || it == sent.end())
            {
                continue;
            }
            std::uint64_t errors = 0;
            for (std::size_t i = 0; i < info.size(); ++i)
            {
                errors += Popcount(static_cast<std::uint8_t>(r.info[i] ^ it->second.data[i]));
            }
            p.bits += info.size() * 8U;
            p.bit_errors += errors;
            p.esn0_sum += static_cast<double>(r.esn0_db);
            ++p.esn0_count;
            if (!r.crc_ok || errors != 0)
            {
                ++p.frame_errors;
            }
            sent.erase(it);
        }
    };
    for (unsigned f = 0; f < o.frames; ++f)
    {
        for (auto &b : info)
        {
            rng ^= rng << 13U;
            rng ^= rng >> 17U;
            rng ^= rng << 5U;
            b = static_cast<std::uint8_t>(rng);
        }
        const auto seq = static_cast<std::uint8_t>((4U + f) & 0x7FU);
        sent[seq] = Sent{f, info};
        link.received.clear();
        link.Send(modcod, SATLINK_FRAME_DATA, info.data());
        ++p.frames;
        // A frame is delivered while the next one is being received.
        evaluate(link.received);
        for (auto it = sent.begin(); it != sent.end();)
        {
            if (it->second.index + 2U < f) // never arrived
            {
                ++p.missed;
                it = sent.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    // One more frame pushes the last data frame out of the receiver.
    link.received.clear();
    link.Send(modcod, SATLINK_FRAME_IDLE, nullptr);
    evaluate(link.received);
    p.missed += static_cast<unsigned>(sent.size());
    p.frame_errors += p.missed;
    return p;
}

bool ParseDouble(const char *text, double &out)
{
    char *end = nullptr;
    out = std::strtod(text, &end);
    return end != text && *end == '\0';
}

} // namespace

int main(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        double v = 0.0;
        if (i + 1 >= argc || !ParseDouble(argv[i + 1], v))
        {
            (void)std::fprintf(stderr,
                               "usage: satlink-modem-perf [--frames N] [--step DB] [--freq CPS] "
                               "[--delay SAMPLES] [--modcod N]\n");
            return 64;
        }
        ++i;
        if (a == "--frames")
            o.frames = static_cast<unsigned>(v);
        else if (a == "--step" && v > 0.0)
            o.step_db = v;
        else if (a == "--freq")
            o.freq_cps = v;
        else if (a == "--delay")
            o.delay = v;
        else if (a == "--modcod")
            o.modcod = static_cast<int>(v);
        else
        {
            (void)std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 64;
        }
    }

    (void)std::printf("modcod,esn0_db,frames,frame_errors,missed,fer,bits,bit_errors,ber,"
                      "esn0_est_db\n");
    for (std::uint8_t m = 0; m < SATLINK_MODCOD_COUNT; ++m)
    {
        if (o.modcod >= 0 && m != o.modcod)
        {
            continue;
        }
        const auto threshold = static_cast<double>(satlink_modcod_get(m)->esn0_threshold_db);
        // From well below the threshold until the frame errors have stopped.
        unsigned clean = 0;
        const auto steps = static_cast<unsigned>(std::lround(13.0 / o.step_db));
        for (unsigned step = 0; step <= steps && clean < 3; ++step)
        {
            const double db = threshold - 5.0 + (step * o.step_db);
            const Point p = Measure(m, db, o);
            const double fer = static_cast<double>(p.frame_errors) / p.frames;
            const double ber = p.bits != 0U
                                   ? static_cast<double>(p.bit_errors) / static_cast<double>(p.bits)
                                   : 0.5;
            const double est = p.esn0_count != 0U ? p.esn0_sum / p.esn0_count : std::nan("");
            (void)std::printf("%u,%.2f,%u,%u,%u,%.6g,%llu,%llu,%.6g,%.2f\n", m, db, p.frames,
                              p.frame_errors, p.missed, fer,
                              static_cast<unsigned long long>(p.bits),
                              static_cast<unsigned long long>(p.bit_errors), ber, est);
            (void)std::fflush(stdout);
            clean = p.frame_errors == 0U ? clean + 1U : 0U;
        }
    }
    return 0;
}
