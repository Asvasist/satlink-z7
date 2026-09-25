/**
 * @file msg.c
 * @implements SRS-AMP-003
 */
#include "satlink/amp/msg.h"

#include "satlink/common/byte_order.h"

#define PING_LEN   (8U)
#define CONFIG_LEN (4U)
#define CHAN_LEN   (4U)
#define ACM_LEN    (7U)
#define TIME_LEN   (8U)
#define RXF_LEN    (5U + SATLINK_MSG_FRAME_BYTES)
#define STATUS_LEN (58U)

size_t satlink_msg_encode_ping(const satlink_msg_ping_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < PING_LEN))
    {
        return 0U;
    }
    satlink_put_le32(&out[0], m->token);
    satlink_put_le32(&out[4], m->uptime_ms);
    return PING_LEN;
}

satlink_status_t satlink_msg_decode_ping(const uint8_t *in, size_t len, satlink_msg_ping_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len != PING_LEN)
    {
        return SATLINK_ERR_RANGE;
    }
    m->token = satlink_get_le32(&in[0]);
    m->uptime_ms = satlink_get_le32(&in[4]);
    return SATLINK_OK;
}

size_t satlink_msg_encode_modem_config(const satlink_msg_modem_config_t *m, uint8_t *out,
                                       size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < CONFIG_LEN))
    {
        return 0U;
    }
    out[0] = m->tx_enable ? 1U : 0U;
    out[1] = m->rx_enable ? 1U : 0U;
    out[2] = m->modcod;
    out[3] = m->loopback;
    return CONFIG_LEN;
}

satlink_status_t satlink_msg_decode_modem_config(const uint8_t *in, size_t len,
                                                 satlink_msg_modem_config_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if ((len != CONFIG_LEN) || (in[3] > (uint8_t)SATLINK_LOOP_SOFTWARE))
    {
        return SATLINK_ERR_RANGE;
    }
    m->tx_enable = in[0] != 0U;
    m->rx_enable = in[1] != 0U;
    m->modcod = in[2];
    m->loopback = in[3];
    return SATLINK_OK;
}

size_t satlink_msg_encode_channel(const satlink_msg_channel_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < CHAN_LEN))
    {
        return 0U;
    }
    satlink_put_le16(&out[0], m->noise_level);
    satlink_put_le16(&out[2], m->gain_q15);
    return CHAN_LEN;
}

satlink_status_t satlink_msg_decode_channel(const uint8_t *in, size_t len, satlink_msg_channel_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len != CHAN_LEN)
    {
        return SATLINK_ERR_RANGE;
    }
    m->noise_level = satlink_get_le16(&in[0]);
    m->gain_q15 = satlink_get_le16(&in[2]);
    return SATLINK_OK;
}

size_t satlink_msg_encode_acm_config(const satlink_msg_acm_config_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < ACM_LEN))
    {
        return 0U;
    }
    out[0] = m->enabled ? 1U : 0U;
    out[1] = m->min_modcod;
    out[2] = m->max_modcod;
    satlink_put_le16(&out[3], (uint16_t)m->margin_cdb);
    satlink_put_le16(&out[5], (uint16_t)m->hysteresis_cdb);
    return ACM_LEN;
}

satlink_status_t satlink_msg_decode_acm_config(const uint8_t *in, size_t len,
                                               satlink_msg_acm_config_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if ((len != ACM_LEN) || (in[1] > in[2]))
    {
        return SATLINK_ERR_RANGE;
    }
    m->enabled = in[0] != 0U;
    m->min_modcod = in[1];
    m->max_modcod = in[2];
    m->margin_cdb = (int16_t)satlink_get_le16(&in[3]);
    m->hysteresis_cdb = (int16_t)satlink_get_le16(&in[5]);
    return SATLINK_OK;
}

size_t satlink_msg_encode_time(const satlink_msg_time_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < TIME_LEN))
    {
        return 0U;
    }
    satlink_put_le32(&out[0], (uint32_t)(m->unix_ms & 0xFFFFFFFFULL));
    satlink_put_le32(&out[4], (uint32_t)(m->unix_ms >> 32U));
    return TIME_LEN;
}

satlink_status_t satlink_msg_decode_time(const uint8_t *in, size_t len, satlink_msg_time_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len != TIME_LEN)
    {
        return SATLINK_ERR_RANGE;
    }
    m->unix_ms = (uint64_t)satlink_get_le32(&in[0]) | ((uint64_t)satlink_get_le32(&in[4]) << 32U);
    return SATLINK_OK;
}

size_t satlink_msg_encode_rx_frame(const satlink_msg_rx_frame_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < RXF_LEN))
    {
        return 0U;
    }
    out[0] = m->modcod;
    out[1] = m->crc_ok ? 1U : 0U;
    satlink_put_le16(&out[2], (uint16_t)m->esn0_cdb);
    out[4] = m->seq;
    for (size_t i = 0U; i < SATLINK_MSG_FRAME_BYTES; ++i)
    {
        out[5U + i] = m->data[i];
    }
    return RXF_LEN;
}

satlink_status_t satlink_msg_decode_rx_frame(const uint8_t *in, size_t len,
                                             satlink_msg_rx_frame_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len != RXF_LEN)
    {
        return SATLINK_ERR_RANGE;
    }
    m->modcod = in[0];
    m->crc_ok = in[1] != 0U;
    m->esn0_cdb = (int16_t)satlink_get_le16(&in[2]);
    m->seq = in[4];
    for (size_t i = 0U; i < SATLINK_MSG_FRAME_BYTES; ++i)
    {
        m->data[i] = in[5U + i];
    }
    return SATLINK_OK;
}

size_t satlink_msg_encode_status(const satlink_msg_status_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < STATUS_LEN))
    {
        return 0U;
    }
    const uint32_t words[12] = {m->uptime_ms,      m->frames_ok,         m->frames_crc_error,
                                m->header_errors,  m->bit_errors,        m->bits_checked,
                                m->tx_data_frames, m->tx_idle_frames,    m->dma_underruns,
                                m->rx_overruns,    m->rx_latency_max_us, m->rx_latency_avg_us};
    for (size_t i = 0U; i < 12U; ++i)
    {
        satlink_put_le32(&out[4U * i], words[i]);
    }
    satlink_put_le16(&out[48], (uint16_t)m->esn0_cdb);
    out[50] = m->locked;
    out[51] = m->tx_modcod;
    out[52] = m->acm_enabled;
    out[53] = m->tx_queue_depth;
    satlink_put_le16(&out[54], m->cpu_load_permille);
    out[56] = 0U;
    out[57] = 0U;
    return STATUS_LEN;
}

satlink_status_t satlink_msg_decode_status(const uint8_t *in, size_t len, satlink_msg_status_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if (len != STATUS_LEN)
    {
        return SATLINK_ERR_RANGE;
    }
    uint32_t *const words[12] = {&m->uptime_ms,      &m->frames_ok,         &m->frames_crc_error,
                                 &m->header_errors,  &m->bit_errors,        &m->bits_checked,
                                 &m->tx_data_frames, &m->tx_idle_frames,    &m->dma_underruns,
                                 &m->rx_overruns,    &m->rx_latency_max_us, &m->rx_latency_avg_us};
    for (size_t i = 0U; i < 12U; ++i)
    {
        *words[i] = satlink_get_le32(&in[4U * i]);
    }
    m->esn0_cdb = (int16_t)satlink_get_le16(&in[48]);
    m->locked = in[50];
    m->tx_modcod = in[51];
    m->acm_enabled = in[52];
    m->tx_queue_depth = in[53];
    m->cpu_load_permille = satlink_get_le16(&in[54]);
    return SATLINK_OK;
}

size_t satlink_msg_encode_log(const satlink_msg_log_t *m, uint8_t *out, size_t cap)
{
    if ((m == NULL) || (out == NULL) || (cap < 1U))
    {
        return 0U;
    }
    size_t n = 0U;
    while ((n < SATLINK_MSG_LOG_MAX) && (m->text[n] != '\0') && ((n + 1U) < cap))
    {
        out[1U + n] = (uint8_t)m->text[n];
        ++n;
    }
    out[0] = m->level;
    return n + 1U;
}

satlink_status_t satlink_msg_decode_log(const uint8_t *in, size_t len, satlink_msg_log_t *m)
{
    if ((in == NULL) || (m == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if ((len < 1U) || (len > (SATLINK_MSG_LOG_MAX + 1U)))
    {
        return SATLINK_ERR_RANGE;
    }
    m->level = in[0];
    for (size_t i = 1U; i < len; ++i)
    {
        m->text[i - 1U] = (char)in[i];
    }
    m->text[len - 1U] = '\0';
    return SATLINK_OK;
}

const char *satlink_msg_name(uint16_t type)
{
    switch (type)
    {
    case SATLINK_MSG_PING:
        return "PING";
    case SATLINK_MSG_PONG:
        return "PONG";
    case SATLINK_MSG_MODEM_CONFIG:
        return "MODEM_CONFIG";
    case SATLINK_MSG_CHANNEL:
        return "CHANNEL";
    case SATLINK_MSG_ACM_CONFIG:
        return "ACM_CONFIG";
    case SATLINK_MSG_TIME:
        return "TIME";
    case SATLINK_MSG_TX_FRAME:
        return "TX_FRAME";
    case SATLINK_MSG_RX_FRAME:
        return "RX_FRAME";
    case SATLINK_MSG_STATUS:
        return "STATUS";
    case SATLINK_MSG_LOG:
        return "LOG";
    default:
        return "?";
    }
}
