/**
 * @file test_modem_app.cpp
 * @brief Core 1 modem application in software loopback: Linux messages in, frames through the
 *        whole physical layer, RX_FRAME / STATUS / LOG messages out.
 *
 * @verifies SRS-MDM-007
 * @verifies SRS-AMP-003
 */
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "satlink/modem_app/modem_app.h"

namespace {

struct Sent
{
    std::uint16_t type;
    std::vector<std::uint8_t> payload;
};

class ModemAppTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        app_ = std::make_unique<satlink_modem_app_t>();
        satlink_modem_app_hw_t hw{};
        hw.ctx = this;
        hw.send_msg = &SendMsg;
        hw.set_channel = &SetChannel;
        hw.set_loopback = &SetLoopback;
        hw.select_modcod = &SelectModcod;
        ASSERT_EQ(SATLINK_OK, satlink_modem_app_init(app_.get(), &hw));
    }

    static satlink_status_t SendMsg(void *ctx, std::uint16_t type, const std::uint8_t *p,
                                    std::uint16_t len)
    {
        static_cast<ModemAppTest *>(ctx)->sent_.push_back({type, {p, p + len}});
        return SATLINK_OK;
    }
    static void SetChannel(void *ctx, std::uint16_t noise, std::uint16_t gain)
    {
        auto *self = static_cast<ModemAppTest *>(ctx);
        self->pl_noise_ = noise;
        self->pl_gain_ = gain;
    }
    static void SetLoopback(void *ctx, std::uint8_t mode)
    {
        static_cast<ModemAppTest *>(ctx)->pl_loopback_ = mode;
    }
    static std::uint8_t SelectModcod(void *ctx, float esn0_db, bool locked, std::uint8_t)
    {
        auto *self = static_cast<ModemAppTest *>(ctx);
        self->acm_calls_++;
        return (locked && esn0_db > 20.0F) ? 4 : 0;
    }

    void Configure(std::uint8_t modcod, satlink_loopback_t loop = SATLINK_LOOP_SOFTWARE)
    {
        const satlink_msg_modem_config_t cfg{true, true, modcod, static_cast<std::uint8_t>(loop)};
        std::uint8_t buf[8];
        const auto n = satlink_msg_encode_modem_config(&cfg, buf, sizeof(buf));
        satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_MODEM_CONFIG, buf,
                                 static_cast<std::uint16_t>(n));
    }

    void QueueFrame(std::uint8_t fill)
    {
        std::vector<std::uint8_t> data(SATLINK_MSG_FRAME_BYTES, fill);
        satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_TX_FRAME, data.data(),
                                 static_cast<std::uint16_t>(data.size()));
    }

    std::vector<satlink_msg_rx_frame_t> RxFrames() const
    {
        std::vector<satlink_msg_rx_frame_t> out;
        for (const auto &s : sent_)
        {
            satlink_msg_rx_frame_t f{};
            if (s.type == SATLINK_MSG_RX_FRAME &&
                satlink_msg_decode_rx_frame(s.payload.data(), s.payload.size(), &f) == SATLINK_OK)
            {
                out.push_back(f);
            }
        }
        return out;
    }

    std::unique_ptr<satlink_modem_app_t> app_;
    std::vector<Sent> sent_;
    std::uint16_t pl_noise_ = 0xFFFF;
    std::uint16_t pl_gain_ = 0;
    std::uint8_t pl_loopback_ = 0xFF;
    int acm_calls_ = 0;
};

TEST_F(ModemAppTest, DataFramesTravelThroughTheSoftwareLoopback)
{
    Configure(2);
    EXPECT_EQ(SATLINK_LOOP_SOFTWARE, pl_loopback_);
    satlink_modem_app_run_loopback(app_.get(), 3000); // idle frames to lock
    for (std::uint8_t i = 0; i < 4; ++i)
    {
        QueueFrame(static_cast<std::uint8_t>(0x40 + i));
    }
    satlink_modem_app_run_loopback(app_.get(), 6 * satlink_frame_symbols(2));
    const auto frames = RxFrames();
    ASSERT_EQ(4U, frames.size());
    for (std::uint8_t i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(frames[i].crc_ok);
        EXPECT_EQ(2, frames[i].modcod);
        EXPECT_EQ(0x40 + i, frames[i].data[0]);
        EXPECT_EQ(0x40 + i, frames[i].data[127]);
        EXPECT_GT(frames[i].esn0_cdb, 2500);
    }
    EXPECT_EQ(4U, app_->counters.tx_data_frames);
    EXPECT_GT(app_->counters.tx_idle_frames, 0U);
}

TEST_F(ModemAppTest, StatusOncePerSecondWithIdleBer)
{
    Configure(1);
    satlink_modem_app_run_loopback(app_.get(), 8000);
    satlink_modem_app_note_latency(app_.get(), 100);
    satlink_modem_app_note_latency(app_.get(), 300);
    satlink_modem_app_note_platform(app_.get(), 123, 4, 5);
    satlink_modem_app_tick(app_.get(), 999);
    satlink_modem_app_tick(app_.get(), 1000);
    satlink_modem_app_tick(app_.get(), 1500);
    int count = 0;
    satlink_msg_status_t st{};
    for (const auto &s : sent_)
    {
        if (s.type == SATLINK_MSG_STATUS)
        {
            ++count;
            ASSERT_EQ(SATLINK_OK,
                      satlink_msg_decode_status(s.payload.data(), s.payload.size(), &st));
        }
    }
    EXPECT_EQ(1, count);
    EXPECT_EQ(1000U, st.uptime_ms);
    EXPECT_GE(st.frames_ok, 5U);
    EXPECT_EQ(st.frames_ok * 1024U, st.bits_checked);
    EXPECT_EQ(0U, st.bit_errors);
    EXPECT_EQ(1, st.locked);
    EXPECT_EQ(300U, st.rx_latency_max_us);
    EXPECT_EQ(200U, st.rx_latency_avg_us);
    EXPECT_EQ(123, st.cpu_load_permille);
    EXPECT_EQ(4U, st.dma_underruns);
}

TEST_F(ModemAppTest, ChannelNoiseDegradesTheLinkAndReachesThePl)
{
    Configure(4); // 8PSK 5/6 needs ~15 dB
    const satlink_msg_channel_t ch{static_cast<std::uint16_t>(4096 * 0.5), 0x7FFF}; // ~6 dB
    std::uint8_t buf[4];
    satlink_msg_encode_channel(&ch, buf, 4);
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_CHANNEL, buf, 4);
    EXPECT_EQ(2048, pl_noise_);
    EXPECT_EQ(0x7FFF, pl_gain_);
    satlink_modem_app_run_loopback(app_.get(), 6000);
    const auto stats = satlink_rx_stats(&app_->rx);
    EXPECT_LT(stats.esn0_db, 9.0F);
    EXPECT_EQ(0U, stats.frames_ok);
}

TEST_F(ModemAppTest, AcmHookChoosesModcodPerFrame)
{
    Configure(0);
    const satlink_msg_acm_config_t acm{true, 0, 4, 100, 50};
    std::uint8_t buf[8];
    satlink_modem_app_on_msg(
        app_.get(), SATLINK_MSG_ACM_CONFIG, buf,
        static_cast<std::uint16_t>(satlink_msg_encode_acm_config(&acm, buf, 8)));
    EXPECT_TRUE(app_->acm_enabled);
    satlink_modem_app_run_loopback(app_.get(), 10000);
    EXPECT_GT(acm_calls_, 2);
    EXPECT_EQ(4, app_->tx_modcod); // clean loopback: the hook raised it
}

TEST_F(ModemAppTest, PingIsAnsweredWithUptime)
{
    satlink_modem_app_tick(app_.get(), 250);
    const satlink_msg_ping_t ping{77, 0};
    std::uint8_t buf[8];
    satlink_msg_encode_ping(&ping, buf, 8);
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_PING, buf, 8);
    ASSERT_FALSE(sent_.empty());
    EXPECT_EQ(SATLINK_MSG_PONG, sent_.back().type);
    satlink_msg_ping_t pong{};
    ASSERT_EQ(SATLINK_OK, satlink_msg_decode_ping(sent_.back().payload.data(), 8, &pong));
    EXPECT_EQ(77U, pong.token);
    EXPECT_EQ(250U, pong.uptime_ms);
}

TEST_F(ModemAppTest, BadMessagesAreCountedAndLogged)
{
    const std::uint8_t junk[3] = {1, 2, 3};
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_PING, junk, 3);
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_TX_FRAME, junk, 3);
    satlink_modem_app_on_msg(app_.get(), 0x1234, junk, 3);
    const std::uint8_t bad_cfg[4] = {1, 1, 9, 0}; // unknown MODCOD
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_MODEM_CONFIG, bad_cfg, 4);
    EXPECT_EQ(4U, app_->counters.msg_errors);
    int logs = 0;
    for (const auto &s : sent_)
    {
        logs += s.type == SATLINK_MSG_LOG ? 1 : 0;
    }
    EXPECT_EQ(4, logs);
}

TEST_F(ModemAppTest, QueueOverflowDropsAndCounts)
{
    for (int i = 0; i < 20; ++i)
    {
        QueueFrame(1);
    }
    EXPECT_EQ(4U, app_->counters.tx_queue_overflows);
    satlink_msg_status_t st{};
    satlink_modem_app_status(app_.get(), &st);
    EXPECT_EQ(16, st.tx_queue_depth);
}

TEST_F(ModemAppTest, TxOffSendsSilenceAndLoopbackOnlyInSoftwareMode)
{
    const satlink_msg_modem_config_t cfg{false, true, 1, SATLINK_LOOP_ANALOG};
    std::uint8_t buf[4];
    satlink_msg_encode_modem_config(&cfg, buf, 4);
    satlink_modem_app_on_msg(app_.get(), SATLINK_MSG_MODEM_CONFIG, buf, 4);
    satlink_cf_t sym[8];
    satlink_modem_app_tx_symbols(app_.get(), sym, 8);
    for (const auto &s : sym)
    {
        EXPECT_EQ(0.0F, s.re);
    }
    satlink_modem_app_run_loopback(app_.get(), 5000);
    EXPECT_EQ(0U, satlink_rx_stats(&app_->rx).frames_ok);
    EXPECT_FLOAT_EQ(0.25F, satlink_modem_app_noise_sigma(1024));
}

} // namespace
