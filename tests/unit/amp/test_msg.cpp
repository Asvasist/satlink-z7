/**
 * @file test_msg.cpp
 * @brief Linux <-> FreeRTOS messages: round trips, wire sizes and validation.
 *
 * @verifies SRS-AMP-003
 */
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

#include "satlink/amp/msg.h"

namespace {

TEST(MsgTest, PingAndTime)
{
    std::uint8_t buf[SATLINK_MSG_MAX_BYTES];
    const satlink_msg_ping_t ping{0xCAFEF00D, 1234};
    const auto n = satlink_msg_encode_ping(&ping, buf, sizeof(buf));
    ASSERT_EQ(8U, n);
    EXPECT_EQ(0x0D, buf[0]);
    satlink_msg_ping_t out{};
    ASSERT_EQ(SATLINK_OK, satlink_msg_decode_ping(buf, n, &out));
    EXPECT_EQ(ping.token, out.token);
    EXPECT_EQ(1234U, out.uptime_ms);
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_msg_decode_ping(buf, n - 1, &out));
    EXPECT_EQ(0U, satlink_msg_encode_ping(&ping, buf, 7));

    const satlink_msg_time_t t{1760000000123ULL};
    satlink_msg_time_t t_out{};
    ASSERT_EQ(SATLINK_OK,
              satlink_msg_decode_time(buf, satlink_msg_encode_time(&t, buf, 8), &t_out));
    EXPECT_EQ(t.unix_ms, t_out.unix_ms);
}

TEST(MsgTest, ConfigChannelAndAcm)
{
    std::uint8_t buf[SATLINK_MSG_MAX_BYTES];
    const satlink_msg_modem_config_t cfg{true, false, 3, SATLINK_LOOP_SOFTWARE};
    satlink_msg_modem_config_t cfg_out{};
    ASSERT_EQ(SATLINK_OK,
              satlink_msg_decode_modem_config(
                  buf, satlink_msg_encode_modem_config(&cfg, buf, sizeof(buf)), &cfg_out));
    EXPECT_TRUE(cfg_out.tx_enable);
    EXPECT_FALSE(cfg_out.rx_enable);
    EXPECT_EQ(3, cfg_out.modcod);
    buf[3] = 9;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_msg_decode_modem_config(buf, 4, &cfg_out));

    const satlink_msg_channel_t ch{500, 0x4000};
    satlink_msg_channel_t ch_out{};
    ASSERT_EQ(SATLINK_OK,
              satlink_msg_decode_channel(buf, satlink_msg_encode_channel(&ch, buf, 4), &ch_out));
    EXPECT_EQ(500, ch_out.noise_level);
    EXPECT_EQ(0x4000, ch_out.gain_q15);

    const satlink_msg_acm_config_t acm{true, 0, 4, -150, 50};
    satlink_msg_acm_config_t acm_out{};
    ASSERT_EQ(SATLINK_OK,
              satlink_msg_decode_acm_config(
                  buf, satlink_msg_encode_acm_config(&acm, buf, sizeof(buf)), &acm_out));
    EXPECT_EQ(-150, acm_out.margin_cdb);
    EXPECT_EQ(4, acm_out.max_modcod);
    buf[1] = 4;
    buf[2] = 1; // min > max
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_msg_decode_acm_config(buf, 7, &acm_out));
}

TEST(MsgTest, RxFrameAndStatus)
{
    std::uint8_t buf[SATLINK_MSG_MAX_BYTES];
    satlink_msg_rx_frame_t f{};
    f.modcod = 2;
    f.crc_ok = true;
    f.esn0_cdb = -123;
    f.seq = 99;
    for (std::size_t i = 0; i < sizeof(f.data); ++i)
    {
        f.data[i] = static_cast<std::uint8_t>(i * 3);
    }
    const auto n = satlink_msg_encode_rx_frame(&f, buf, sizeof(buf));
    ASSERT_EQ(133U, n);
    satlink_msg_rx_frame_t out{};
    ASSERT_EQ(SATLINK_OK, satlink_msg_decode_rx_frame(buf, n, &out));
    EXPECT_EQ(-123, out.esn0_cdb);
    EXPECT_EQ(99, out.seq);
    EXPECT_EQ(0, std::memcmp(f.data, out.data, sizeof(f.data)));

    satlink_msg_status_t s{};
    s.uptime_ms = 1;
    s.frames_ok = 2;
    s.rx_latency_avg_us = 12;
    s.esn0_cdb = 1234;
    s.locked = 1;
    s.tx_modcod = 4;
    s.cpu_load_permille = 321;
    const auto sn = satlink_msg_encode_status(&s, buf, sizeof(buf));
    ASSERT_EQ(58U, sn);
    satlink_msg_status_t s_out{};
    ASSERT_EQ(SATLINK_OK, satlink_msg_decode_status(buf, sn, &s_out));
    EXPECT_EQ(2U, s_out.frames_ok);
    EXPECT_EQ(12U, s_out.rx_latency_avg_us);
    EXPECT_EQ(1234, s_out.esn0_cdb);
    EXPECT_EQ(4, s_out.tx_modcod);
    EXPECT_EQ(321, s_out.cpu_load_permille);
}

TEST(MsgTest, LogTruncatesAndTerminates)
{
    std::uint8_t buf[SATLINK_MSG_MAX_BYTES];
    satlink_msg_log_t log{};
    log.level = 2;
    std::memset(log.text, 'x', SATLINK_MSG_LOG_MAX);
    log.text[SATLINK_MSG_LOG_MAX] = '\0';
    const auto n = satlink_msg_encode_log(&log, buf, sizeof(buf));
    EXPECT_EQ(SATLINK_MSG_LOG_MAX + 1U, n);
    satlink_msg_log_t out{};
    ASSERT_EQ(SATLINK_OK, satlink_msg_decode_log(buf, n, &out));
    EXPECT_EQ(SATLINK_MSG_LOG_MAX, std::strlen(out.text));
    EXPECT_EQ(2, out.level);
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_msg_decode_log(buf, 0, &out));
}

TEST(MsgTest, Names)
{
    EXPECT_STREQ("RX_FRAME", satlink_msg_name(SATLINK_MSG_RX_FRAME));
    EXPECT_STREQ("?", satlink_msg_name(0x7777));
}

} // namespace
