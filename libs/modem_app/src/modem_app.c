/**
 * @file modem_app.c
 * @implements SRS-MDM-007
 */
#include "satlink/modem_app/modem_app.h"

#include <math.h>

#include "satlink/modem/modcod.h"

#define STATUS_PERIOD_MS (1000U)

static void log_msg(satlink_modem_app_t *app, uint8_t level, const char *text)
{
    satlink_msg_log_t log;
    log.level = level;
    size_t i = 0U;
    /* The terminator ends the loop before the bound for every caller's literal. */
    // cppcheck-suppress arrayIndexOutOfBoundsCond
    for (; (i < SATLINK_MSG_LOG_MAX) && (text[i] != '\0'); ++i)
    {
        log.text[i] = text[i];
    }
    log.text[i] = '\0';
    uint8_t buf[SATLINK_MSG_MAX_BYTES];
    const size_t n = satlink_msg_encode_log(&log, buf, sizeof(buf));
    (void)app->hw.send_msg(app->hw.ctx, (uint16_t)SATLINK_MSG_LOG, buf, (uint16_t)n);
}

static void on_rx_frame(void *ctx, const satlink_rx_frame_t *frame)
{
    satlink_modem_app_t *app = (satlink_modem_app_t *)ctx;
    /* IDLE frames only feed the statistics; DATA frames go to Linux, good or bad (the payload
     * manager counts CRC failures per virtual channel). */
    if (frame->header.type != (uint8_t)SATLINK_FRAME_DATA)
    {
        return;
    }
    satlink_msg_rx_frame_t m;
    m.modcod = frame->header.modcod;
    m.crc_ok = frame->crc_ok;
    m.esn0_cdb = (int16_t)lrintf(frame->esn0_db * 100.0F);
    m.seq = frame->header.seq;
    for (size_t i = 0U; i < SATLINK_MSG_FRAME_BYTES; ++i)
    {
        m.data[i] = frame->info[i];
    }
    uint8_t buf[SATLINK_MSG_MAX_BYTES];
    const size_t n = satlink_msg_encode_rx_frame(&m, buf, sizeof(buf));
    if (app->hw.send_msg(app->hw.ctx, (uint16_t)SATLINK_MSG_RX_FRAME, buf, (uint16_t)n) ==
        SATLINK_OK)
    {
        ++app->counters.rx_frames_forwarded;
    }
}

float satlink_modem_app_noise_sigma(uint16_t noise_level)
{
    return (float)noise_level / 4096.0F;
}

static void apply_channel(satlink_modem_app_t *app)
{
    app->loop_channel.noise_sigma = satlink_modem_app_noise_sigma(app->channel_cfg.noise_level);
    app->loop_channel.gain = (float)app->channel_cfg.gain_q15 / 32767.0F;
    if (app->hw.set_channel != NULL)
    {
        app->hw.set_channel(app->hw.ctx, app->channel_cfg.noise_level, app->channel_cfg.gain_q15);
    }
}

satlink_status_t satlink_modem_app_init(satlink_modem_app_t *app, const satlink_modem_app_hw_t *hw)
{
    if ((app == NULL) || (hw == NULL) || (hw->send_msg == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    app->hw = *hw;
    app->config.tx_enable = true;
    app->config.rx_enable = true;
    app->config.modcod = 1U;
    app->config.loopback = (uint8_t)SATLINK_LOOP_ANALOG;
    app->channel_cfg.noise_level = 0U;
    app->channel_cfg.gain_q15 = 0x7FFFU;
    app->acm_enabled = false;
    app->q_head = 0U;
    app->q_tail = 0U;
    app->tx_len = 0U;
    app->tx_pos = 0U;
    app->tx_seq = 0U;
    app->tx_modcod = app->config.modcod;
    satlink_rx_init(&app->rx, &on_rx_frame, app);
    satlink_fir_init_rrc(&app->loop_tx_fir);
    satlink_fir_init_rrc(&app->loop_rx_fir);
    satlink_channel_init(&app->loop_channel, 0x5EED1234U);
    app->uptime_ms = 0U;
    app->next_status_ms = STATUS_PERIOD_MS;
    app->unix_offset_ms = 0U;
    app->latency_max_us = 0U;
    app->latency_sum_us = 0U;
    app->latency_count = 0U;
    app->cpu_load_permille = 0U;
    app->dma_underruns = 0U;
    app->rx_overruns = 0U;
    app->counters.tx_data_frames = 0U;
    app->counters.tx_idle_frames = 0U;
    app->counters.tx_queue_overflows = 0U;
    app->counters.rx_frames_forwarded = 0U;
    app->counters.msg_errors = 0U;
    apply_channel(app);
    return SATLINK_OK;
}

static uint32_t queue_depth(const satlink_modem_app_t *app)
{
    return app->q_head - app->q_tail;
}

static void bad_msg(satlink_modem_app_t *app, const char *what)
{
    ++app->counters.msg_errors;
    log_msg(app, 2U, what);
}

void satlink_modem_app_on_msg(satlink_modem_app_t *app, uint16_t type, const uint8_t *payload,
                              uint16_t len)
{
    switch (type)
    {
    case SATLINK_MSG_PING:
    {
        uint8_t buf[SATLINK_MSG_MAX_BYTES];
        satlink_msg_ping_t ping;
        if (satlink_msg_decode_ping(payload, len, &ping) != SATLINK_OK)
        {
            bad_msg(app, "bad PING");
            break;
        }
        ping.uptime_ms = app->uptime_ms;
        const size_t n = satlink_msg_encode_ping(&ping, buf, sizeof(buf));
        (void)app->hw.send_msg(app->hw.ctx, (uint16_t)SATLINK_MSG_PONG, buf, (uint16_t)n);
        break;
    }
    case SATLINK_MSG_MODEM_CONFIG:
    {
        satlink_msg_modem_config_t cfg;
        if ((satlink_msg_decode_modem_config(payload, len, &cfg) != SATLINK_OK) ||
            (satlink_modcod_get(cfg.modcod) == NULL))
        {
            bad_msg(app, "bad MODEM_CONFIG");
            break;
        }
        app->config = cfg;
        if (app->hw.set_loopback != NULL)
        {
            app->hw.set_loopback(app->hw.ctx, cfg.loopback);
        }
        log_msg(app, 1U, "modem configured");
        break;
    }
    case SATLINK_MSG_CHANNEL:
        if (satlink_msg_decode_channel(payload, len, &app->channel_cfg) != SATLINK_OK)
        {
            bad_msg(app, "bad CHANNEL");
            break;
        }
        apply_channel(app);
        break;
    case SATLINK_MSG_ACM_CONFIG:
    {
        satlink_msg_acm_config_t acm;
        if (satlink_msg_decode_acm_config(payload, len, &acm) != SATLINK_OK)
        {
            bad_msg(app, "bad ACM_CONFIG");
            break;
        }
        app->acm_enabled = acm.enabled && (app->hw.select_modcod != NULL);
        break;
    }
    case SATLINK_MSG_TIME:
    {
        satlink_msg_time_t t;
        if (satlink_msg_decode_time(payload, len, &t) != SATLINK_OK)
        {
            bad_msg(app, "bad TIME");
            break;
        }
        app->unix_offset_ms = t.unix_ms - (uint64_t)app->uptime_ms;
        break;
    }
    case SATLINK_MSG_TX_FRAME:
        if ((payload == NULL) || (len != SATLINK_MSG_FRAME_BYTES))
        {
            bad_msg(app, "bad TX_FRAME");
            break;
        }
        if (queue_depth(app) >= SATLINK_MODEM_APP_TX_QUEUE)
        {
            ++app->counters.tx_queue_overflows;
            break;
        }
        for (size_t i = 0U; i < SATLINK_FRAME_INFO_BYTES; ++i)
        {
            app->queue[app->q_head % SATLINK_MODEM_APP_TX_QUEUE][i] = payload[i];
        }
        ++app->q_head;
        break;
    default:
        bad_msg(app, "unknown message");
        break;
    }
}

static void next_frame(satlink_modem_app_t *app)
{
    const satlink_rx_stats_t stats = satlink_rx_stats(&app->rx);
    uint8_t modcod = app->config.modcod;
    if (app->acm_enabled)
    {
        modcod = app->hw.select_modcod(app->hw.ctx, stats.esn0_db, stats.locked, app->tx_modcod);
        if (satlink_modcod_get(modcod) == NULL)
        {
            modcod = 0U;
        }
    }
    app->tx_modcod = modcod;

    satlink_frame_header_t header;
    header.modcod = modcod;
    header.seq = app->tx_seq;
    app->tx_seq = (uint8_t)((app->tx_seq + 1U) & 0x7FU);
    const uint8_t *info = NULL;
    if (queue_depth(app) > 0U)
    {
        header.type = (uint8_t)SATLINK_FRAME_DATA;
        info = app->queue[app->q_tail % SATLINK_MODEM_APP_TX_QUEUE];
        ++app->counters.tx_data_frames;
    }
    else
    {
        header.type = (uint8_t)SATLINK_FRAME_IDLE;
        ++app->counters.tx_idle_frames;
    }
    size_t n = 0U;
    (void)satlink_frame_build(&header, info, app->tx_frame, SATLINK_FRAME_MAX_SYMBOLS, &n);
    if (info != NULL)
    {
        ++app->q_tail;
    }
    app->tx_len = n;
    app->tx_pos = 0U;
}

void satlink_modem_app_tx_symbols(satlink_modem_app_t *app, satlink_cf_t *out, size_t count)
{
    for (size_t i = 0U; i < count; ++i)
    {
        if (!app->config.tx_enable)
        {
            out[i].re = 0.0F;
            out[i].im = 0.0F;
            continue;
        }
        if (app->tx_pos >= app->tx_len)
        {
            next_frame(app);
        }
        out[i] = app->tx_frame[app->tx_pos];
        ++app->tx_pos;
    }
}

void satlink_modem_app_rx_samples(satlink_modem_app_t *app, const satlink_cf_t *in, size_t count)
{
    if (app->config.rx_enable)
    {
        satlink_rx_push(&app->rx, in, count);
    }
}

void satlink_modem_app_run_loopback(satlink_modem_app_t *app, size_t symbols)
{
    if (app->config.loopback != (uint8_t)SATLINK_LOOP_SOFTWARE)
    {
        return;
    }
    satlink_cf_t sym[SATLINK_MODEM_APP_LOOP_SYMBOLS];
    satlink_cf_t samples[SATLINK_MODEM_APP_LOOP_SYMBOLS * SATLINK_MODEM_SPS];
    size_t left = symbols;
    while (left > 0U)
    {
        const size_t n =
            (left < SATLINK_MODEM_APP_LOOP_SYMBOLS) ? left : SATLINK_MODEM_APP_LOOP_SYMBOLS;
        satlink_modem_app_tx_symbols(app, sym, n);
        satlink_rrc_interpolate(&app->loop_tx_fir, sym, n, samples);
        const size_t ns = n * SATLINK_MODEM_SPS;
        satlink_channel_apply(&app->loop_channel, samples, ns);
        for (size_t i = 0U; i < ns; ++i)
        {
            samples[i] = satlink_fir_push(&app->loop_rx_fir, samples[i]);
        }
        satlink_modem_app_rx_samples(app, samples, ns);
        left -= n;
    }
}

void satlink_modem_app_note_latency(satlink_modem_app_t *app, uint32_t us)
{
    if (us > app->latency_max_us)
    {
        app->latency_max_us = us;
    }
    app->latency_sum_us += us;
    ++app->latency_count;
}

void satlink_modem_app_note_platform(satlink_modem_app_t *app, uint16_t cpu_load_permille,
                                     uint32_t dma_underruns, uint32_t rx_overruns)
{
    app->cpu_load_permille = cpu_load_permille;
    app->dma_underruns = dma_underruns;
    app->rx_overruns = rx_overruns;
}

void satlink_modem_app_status(const satlink_modem_app_t *app, satlink_msg_status_t *status)
{
    const satlink_rx_stats_t rx = satlink_rx_stats(&app->rx);
    status->uptime_ms = app->uptime_ms;
    status->frames_ok = rx.frames_ok;
    status->frames_crc_error = rx.frames_crc_error;
    status->header_errors = rx.header_errors;
    status->bit_errors = rx.bit_errors;
    status->bits_checked = rx.bits_checked;
    status->tx_data_frames = app->counters.tx_data_frames;
    status->tx_idle_frames = app->counters.tx_idle_frames;
    status->dma_underruns = app->dma_underruns;
    status->rx_overruns = app->rx_overruns;
    status->rx_latency_max_us = app->latency_max_us;
    status->rx_latency_avg_us =
        (app->latency_count > 0U) ? (app->latency_sum_us / app->latency_count) : 0U;
    status->esn0_cdb = (int16_t)lrintf(rx.esn0_db * 100.0F);
    status->locked = rx.locked ? 1U : 0U;
    status->tx_modcod = app->tx_modcod;
    status->acm_enabled = app->acm_enabled ? 1U : 0U;
    status->tx_queue_depth = (uint8_t)queue_depth(app);
    status->cpu_load_permille = app->cpu_load_permille;
}

void satlink_modem_app_tick(satlink_modem_app_t *app, uint32_t now_ms)
{
    app->uptime_ms = now_ms;
    if ((int32_t)(now_ms - app->next_status_ms) < 0)
    {
        return;
    }
    app->next_status_ms = now_ms + STATUS_PERIOD_MS;
    satlink_msg_status_t status;
    satlink_modem_app_status(app, &status);
    uint8_t buf[SATLINK_MSG_MAX_BYTES];
    const size_t n = satlink_msg_encode_status(&status, buf, sizeof(buf));
    (void)app->hw.send_msg(app->hw.ctx, (uint16_t)SATLINK_MSG_STATUS, buf, (uint16_t)n);
}
