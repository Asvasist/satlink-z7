/**
 * @file can_boot.c
 * @brief CAN firmware upload protocol: receiver and sender state machines.
 *
 * @implements SRS-HKC-003
 */
#include "satlink/boot/can_boot.h"

#include "satlink/common/crc16_ccitt.h"

#define CANBOOT_RSP_DLC (8U)

/* ---------------------------------------------------------------------------------------- */
/* Frame helpers                                                                            */
/* ---------------------------------------------------------------------------------------- */

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8U));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8U);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static void frame_clear(satlink_can_frame_t *frame, uint32_t id, uint8_t dlc)
{
    frame->id = id;
    frame->dlc = dlc;
    frame->extended = false;
    frame->rtr = false;
    for (size_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = 0U;
    }
}

static bool is_command(const satlink_can_frame_t *frame)
{
    return (frame->id == SATLINK_CANBOOT_ID_CMD) && !frame->extended && !frame->rtr &&
           (frame->dlc >= 1U);
}

static bool is_response(const satlink_can_frame_t *frame)
{
    return (frame->id == SATLINK_CANBOOT_ID_RSP) && !frame->extended && !frame->rtr &&
           (frame->dlc == CANBOOT_RSP_DLC);
}

static void make_response(satlink_can_frame_t *rsp, satlink_canboot_rsp_t kind, uint8_t arg,
                          uint16_t value, uint32_t aux)
{
    frame_clear(rsp, SATLINK_CANBOOT_ID_RSP, (uint8_t)CANBOOT_RSP_DLC);
    rsp->data[0] = (uint8_t)kind;
    rsp->data[1] = arg;
    put_u16(&rsp->data[2], value);
    put_u32(&rsp->data[4], aux);
}

void satlink_canboot_make_enter(satlink_can_frame_t *frame)
{
    if (frame != NULL)
    {
        frame_clear(frame, SATLINK_CANBOOT_ID_CMD, 1U);
        frame->data[0] = (uint8_t)SATLINK_CANBOOT_OP_ENTER;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* Receiver                                                                                 */
/* ---------------------------------------------------------------------------------------- */

satlink_status_t satlink_canboot_rx_init(satlink_canboot_rx_t *rx,
                                         const satlink_canboot_rx_config_t *cfg)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((rx != NULL) && (cfg != NULL) && (cfg->write != NULL))
    {
        if ((cfg->max_size == 0U) || (cfg->max_size > SATLINK_CANBOOT_MAX_IMAGE))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            rx->cfg = *cfg;
            if (rx->cfg.window == 0U)
            {
                rx->cfg.window = (uint16_t)SATLINK_CANBOOT_DEFAULT_WINDOW;
            }
            rx->state = SATLINK_CANBOOT_RX_IDLE;
            rx->size = 0U;
            rx->crc_expected = 0U;
            rx->crc_running = (uint16_t)SATLINK_CRC16_CCITT_INIT;
            rx->received = 0U;
            rx->next_seq = 0U;
            rx->since_ack = 0U;
            rx->boot_requested = false;
            status = SATLINK_OK;
        }
    }

    return status;
}

static void rx_nak(const satlink_canboot_rx_t *rx, satlink_can_frame_t *rsp, bool *have_rsp,
                   uint8_t op, satlink_canboot_err_t err)
{
    make_response(rsp, SATLINK_CANBOOT_RSP_NAK, op, (uint16_t)err, (uint32_t)rx->next_seq);
    *have_rsp = true;
}

static void rx_ack(const satlink_canboot_rx_t *rx, satlink_can_frame_t *rsp, bool *have_rsp,
                   uint8_t op)
{
    make_response(rsp, SATLINK_CANBOOT_RSP_ACK, op, rx->next_seq, rx->cfg.max_size);
    *have_rsp = true;
}

static void rx_on_begin(satlink_canboot_rx_t *rx, const satlink_can_frame_t *frame,
                        satlink_can_frame_t *rsp, bool *have_rsp)
{
    if (frame->dlc != 7U)
    {
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_BEGIN, SATLINK_CANBOOT_ERR_LENGTH);
    }
    else
    {
        const uint32_t size = get_u32(&frame->data[1]);

        if ((size == 0U) || (size > rx->cfg.max_size))
        {
            rx->state = SATLINK_CANBOOT_RX_IDLE;
            rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_BEGIN, SATLINK_CANBOOT_ERR_SIZE);
        }
        else
        {
            rx->size = size;
            rx->crc_expected = get_u16(&frame->data[5]);
            rx->crc_running = (uint16_t)SATLINK_CRC16_CCITT_INIT;
            rx->received = 0U;
            rx->next_seq = 0U;
            rx->since_ack = 0U;
            rx->boot_requested = false;
            rx->state = SATLINK_CANBOOT_RX_RECEIVING;
            rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_BEGIN);
        }
    }
}

static void rx_on_data(satlink_canboot_rx_t *rx, const satlink_can_frame_t *frame,
                       satlink_can_frame_t *rsp, bool *have_rsp)
{
    if (rx->state != SATLINK_CANBOOT_RX_RECEIVING)
    {
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA, SATLINK_CANBOOT_ERR_STATE);
    }
    else if (frame->dlc < 3U)
    {
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA, SATLINK_CANBOOT_ERR_LENGTH);
    }
    else
    {
        const uint16_t seq = get_u16(&frame->data[1]);
        const size_t len = (size_t)frame->dlc - 3U;

        if (seq < rx->next_seq)
        {
            /* A retransmission of something already stored: just say where we are. */
            rx->since_ack = 0U;
            rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA);
        }
        else if (seq > rx->next_seq)
        {
            rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA, SATLINK_CANBOOT_ERR_SEQ);
        }
        else
        {
            const uint32_t offset = (uint32_t)seq * SATLINK_CANBOOT_CHUNK;
            const uint32_t remaining = rx->size - rx->received;
            const uint32_t expected =
                (remaining < SATLINK_CANBOOT_CHUNK) ? remaining : SATLINK_CANBOOT_CHUNK;

            if ((offset >= rx->size) || ((uint32_t)len != expected))
            {
                rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA,
                       SATLINK_CANBOOT_ERR_LENGTH);
            }
            else if (rx->cfg.write(rx->cfg.ctx, offset, &frame->data[3], len) != SATLINK_OK)
            {
                rx->state = SATLINK_CANBOOT_RX_ERROR;
                rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA,
                       SATLINK_CANBOOT_ERR_WRITE);
            }
            else
            {
                rx->crc_running = satlink_crc16_ccitt_update(rx->crc_running, &frame->data[3], len);
                rx->received += (uint32_t)len;
                rx->next_seq = (uint16_t)(rx->next_seq + 1U);
                rx->since_ack = (uint16_t)(rx->since_ack + 1U);

                if ((rx->received == rx->size) || (rx->since_ack >= rx->cfg.window))
                {
                    rx->since_ack = 0U;
                    rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_DATA);
                }
            }
        }
    }
}

static void rx_on_end(satlink_canboot_rx_t *rx, satlink_can_frame_t *rsp, bool *have_rsp)
{
    if (rx->state == SATLINK_CANBOOT_RX_VERIFIED)
    {
        rx_ack(rx, rsp, have_rsp,
               (uint8_t)SATLINK_CANBOOT_OP_END); /* repeated END: the ACK was lost */
    }
    else if (rx->state != SATLINK_CANBOOT_RX_RECEIVING)
    {
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_END, SATLINK_CANBOOT_ERR_STATE);
    }
    else if (rx->received != rx->size)
    {
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_END, SATLINK_CANBOOT_ERR_INCOMPLETE);
    }
    else if (rx->crc_running != rx->crc_expected)
    {
        rx->state = SATLINK_CANBOOT_RX_ERROR;
        rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_END, SATLINK_CANBOOT_ERR_CRC);
    }
    else
    {
        rx->state = SATLINK_CANBOOT_RX_VERIFIED;
        rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_END);
    }
}

satlink_status_t satlink_canboot_rx_handle(satlink_canboot_rx_t *rx,
                                           const satlink_can_frame_t *frame,
                                           satlink_can_frame_t *rsp, bool *have_rsp)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((rx != NULL) && (frame != NULL) && (rsp != NULL) && (have_rsp != NULL))
    {
        status = SATLINK_OK;
        *have_rsp = false;

        if (is_command(frame))
        {
            switch (frame->data[0])
            {
            case (uint8_t)SATLINK_CANBOOT_OP_PING:
                make_response(rsp, SATLINK_CANBOOT_RSP_PONG, (uint8_t)rx->state,
                              (uint16_t)SATLINK_CANBOOT_VERSION, rx->cfg.max_size);
                *have_rsp = true;
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_BEGIN:
                rx_on_begin(rx, frame, rsp, have_rsp);
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_DATA:
                rx_on_data(rx, frame, rsp, have_rsp);
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_END:
                rx_on_end(rx, rsp, have_rsp);
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_BOOT:
                if (rx->state == SATLINK_CANBOOT_RX_VERIFIED)
                {
                    rx->boot_requested = true;
                    rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_BOOT);
                }
                else
                {
                    rx_nak(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_BOOT,
                           SATLINK_CANBOOT_ERR_STATE);
                }
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_ENTER:
                rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_ENTER); /* already here */
                break;
            case (uint8_t)SATLINK_CANBOOT_OP_ABORT:
                rx->state = SATLINK_CANBOOT_RX_IDLE;
                rx->size = 0U;
                rx->received = 0U;
                rx->next_seq = 0U;
                rx->since_ack = 0U;
                rx->boot_requested = false;
                rx_ack(rx, rsp, have_rsp, (uint8_t)SATLINK_CANBOOT_OP_ABORT);
                break;
            default:
                rx_nak(rx, rsp, have_rsp, frame->data[0], SATLINK_CANBOOT_ERR_OPCODE);
                break;
            }
        }
    }

    return status;
}

satlink_canboot_rx_state_t satlink_canboot_rx_state(const satlink_canboot_rx_t *rx)
{
    return (rx != NULL) ? rx->state : SATLINK_CANBOOT_RX_IDLE;
}

bool satlink_canboot_rx_boot_requested(const satlink_canboot_rx_t *rx)
{
    return (rx != NULL) && rx->boot_requested;
}

uint32_t satlink_canboot_rx_image_size(const satlink_canboot_rx_t *rx)
{
    return (rx != NULL) ? rx->size : 0U;
}

/* ---------------------------------------------------------------------------------------- */
/* Sender                                                                                   */
/* ---------------------------------------------------------------------------------------- */

satlink_status_t satlink_canboot_tx_init(satlink_canboot_tx_t *tx, const uint8_t *image,
                                         uint32_t size, uint16_t window, bool boot_after,
                                         uint8_t max_retries)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((tx != NULL) && (image != NULL))
    {
        if ((size == 0U) || (size > SATLINK_CANBOOT_MAX_IMAGE))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            tx->image = image;
            tx->size = size;
            tx->crc = satlink_crc16_ccitt(image, (size_t)size);
            tx->window = (window == 0U) ? (uint16_t)SATLINK_CANBOOT_DEFAULT_WINDOW : window;
            tx->total_frames =
                (uint16_t)((size + SATLINK_CANBOOT_CHUNK - 1U) / SATLINK_CANBOOT_CHUNK);
            tx->acked_seq = 0U;
            tx->next_seq = 0U;
            tx->retries = 0U;
            tx->max_retries = max_retries;
            tx->boot_after = boot_after;
            tx->control_pending = true;
            tx->state = SATLINK_CANBOOT_TX_BEGIN;
            tx->error = 0U;
            status = SATLINK_OK;
        }
    }

    return status;
}

static void tx_make_control(const satlink_canboot_tx_t *tx, satlink_can_frame_t *frame,
                            satlink_canboot_op_t op)
{
    if (op == SATLINK_CANBOOT_OP_BEGIN)
    {
        frame_clear(frame, SATLINK_CANBOOT_ID_CMD, 7U);
        frame->data[0] = (uint8_t)op;
        put_u32(&frame->data[1], tx->size);
        put_u16(&frame->data[5], tx->crc);
    }
    else
    {
        frame_clear(frame, SATLINK_CANBOOT_ID_CMD, 1U);
        frame->data[0] = (uint8_t)op;
    }
}

bool satlink_canboot_tx_next(satlink_canboot_tx_t *tx, satlink_can_frame_t *frame)
{
    bool produced = false;

    if ((tx != NULL) && (frame != NULL))
    {
        switch (tx->state)
        {
        case SATLINK_CANBOOT_TX_BEGIN:
        case SATLINK_CANBOOT_TX_END:
        case SATLINK_CANBOOT_TX_BOOT:
            if (tx->control_pending)
            {
                satlink_canboot_op_t op = SATLINK_CANBOOT_OP_BOOT;

                if (tx->state == SATLINK_CANBOOT_TX_BEGIN)
                {
                    op = SATLINK_CANBOOT_OP_BEGIN;
                }
                else if (tx->state == SATLINK_CANBOOT_TX_END)
                {
                    op = SATLINK_CANBOOT_OP_END;
                }
                else
                {
                    op = SATLINK_CANBOOT_OP_BOOT;
                }

                tx_make_control(tx, frame, op);
                tx->control_pending = false;
                produced = true;
            }
            break;
        case SATLINK_CANBOOT_TX_DATA:
            if ((tx->next_seq < tx->total_frames) &&
                ((uint16_t)(tx->next_seq - tx->acked_seq) < tx->window))
            {
                const uint32_t offset = (uint32_t)tx->next_seq * SATLINK_CANBOOT_CHUNK;
                const uint32_t remaining = tx->size - offset;
                const uint32_t len =
                    (remaining < SATLINK_CANBOOT_CHUNK) ? remaining : SATLINK_CANBOOT_CHUNK;

                frame_clear(frame, SATLINK_CANBOOT_ID_CMD, (uint8_t)(3U + len));
                frame->data[0] = (uint8_t)SATLINK_CANBOOT_OP_DATA;
                put_u16(&frame->data[1], tx->next_seq);
                for (uint32_t i = 0U; i < len; ++i)
                {
                    frame->data[3U + i] = tx->image[offset + i];
                }
                tx->next_seq = (uint16_t)(tx->next_seq + 1U);
                produced = true;
            }
            break;
        default:
            break;
        }
    }

    return produced;
}

static void tx_fail(satlink_canboot_tx_t *tx, uint16_t error)
{
    tx->state = SATLINK_CANBOOT_TX_FAILED;
    tx->error = error;
}

satlink_status_t satlink_canboot_tx_on_response(satlink_canboot_tx_t *tx,
                                                const satlink_can_frame_t *frame)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((tx != NULL) && (frame != NULL))
    {
        status = SATLINK_OK;

        if (is_response(frame) && (tx->state != SATLINK_CANBOOT_TX_DONE) &&
            (tx->state != SATLINK_CANBOOT_TX_FAILED))
        {
            const uint8_t kind = frame->data[0];
            const uint8_t arg = frame->data[1];
            const uint16_t value = get_u16(&frame->data[2]);
            const uint32_t aux = get_u32(&frame->data[4]);

            if (kind == (uint8_t)SATLINK_CANBOOT_RSP_ACK)
            {
                if ((tx->state == SATLINK_CANBOOT_TX_BEGIN) &&
                    (arg == (uint8_t)SATLINK_CANBOOT_OP_BEGIN))
                {
                    tx->state = SATLINK_CANBOOT_TX_DATA;
                    tx->retries = 0U;
                }
                else if ((tx->state == SATLINK_CANBOOT_TX_DATA) &&
                         (arg == (uint8_t)SATLINK_CANBOOT_OP_DATA))
                {
                    if (value > tx->total_frames)
                    {
                        tx_fail(tx, (uint16_t)SATLINK_CANBOOT_ERR_SEQ); /* beyond the image */
                    }
                    else if (value > tx->acked_seq)
                    {
                        tx->acked_seq = value;
                        if (tx->next_seq < value)
                        {
                            /* The node is ahead of a rewound sender: skip what it already has. */
                            tx->next_seq = value;
                        }
                        tx->retries = 0U;
                        if (tx->acked_seq == tx->total_frames)
                        {
                            tx->state = SATLINK_CANBOOT_TX_END;
                            tx->control_pending = true;
                        }
                    }
                    else
                    {
                        /* Not newer than what is already acknowledged: a stale repeat. */
                    }
                }
                else if ((tx->state == SATLINK_CANBOOT_TX_END) &&
                         (arg == (uint8_t)SATLINK_CANBOOT_OP_END))
                {
                    tx->retries = 0U;
                    if (tx->boot_after)
                    {
                        tx->state = SATLINK_CANBOOT_TX_BOOT;
                        tx->control_pending = true;
                    }
                    else
                    {
                        tx->state = SATLINK_CANBOOT_TX_DONE;
                    }
                }
                else if ((tx->state == SATLINK_CANBOOT_TX_BOOT) &&
                         (arg == (uint8_t)SATLINK_CANBOOT_OP_BOOT))
                {
                    tx->state = SATLINK_CANBOOT_TX_DONE;
                }
                else
                {
                    /* An acknowledgement for something that is not outstanding: ignore. */
                }
            }
            else if (kind == (uint8_t)SATLINK_CANBOOT_RSP_NAK)
            {
                if ((tx->state == SATLINK_CANBOOT_TX_DATA) &&
                    (value == (uint16_t)SATLINK_CANBOOT_ERR_SEQ) && (aux <= tx->total_frames))
                {
                    /* A gap: everything before aux is stored, resend from there. */
                    if (aux > tx->acked_seq)
                    {
                        tx->acked_seq = (uint16_t)aux;
                    }
                    tx->next_seq = (uint16_t)aux;
                }
                else
                {
                    tx_fail(tx, value);
                }
            }
            else
            {
                /* PONG and anything else carry no transfer information. */
            }
        }
    }

    return status;
}

void satlink_canboot_tx_on_timeout(satlink_canboot_tx_t *tx)
{
    if ((tx != NULL) && (tx->state != SATLINK_CANBOOT_TX_DONE) &&
        (tx->state != SATLINK_CANBOOT_TX_FAILED))
    {
        if (tx->retries >= tx->max_retries)
        {
            tx_fail(tx, (uint16_t)SATLINK_CANBOOT_ERR_TIMEOUT);
        }
        else
        {
            tx->retries = (uint8_t)(tx->retries + 1U);
            if (tx->state == SATLINK_CANBOOT_TX_DATA)
            {
                tx->next_seq = tx->acked_seq;
            }
            else
            {
                tx->control_pending = true;
            }
        }
    }
}

satlink_canboot_tx_state_t satlink_canboot_tx_state(const satlink_canboot_tx_t *tx)
{
    return (tx != NULL) ? tx->state : SATLINK_CANBOOT_TX_FAILED;
}

uint16_t satlink_canboot_tx_error(const satlink_canboot_tx_t *tx)
{
    return (tx != NULL) ? tx->error : 0U;
}

uint8_t satlink_canboot_tx_progress(const satlink_canboot_tx_t *tx)
{
    uint8_t percent = 0U;

    if ((tx != NULL) && (tx->total_frames > 0U))
    {
        percent = (uint8_t)(((uint32_t)tx->acked_seq * 100U) / (uint32_t)tx->total_frames);
        if (tx->state == SATLINK_CANBOOT_TX_DONE)
        {
            percent = 100U;
        }
    }

    return percent;
}
