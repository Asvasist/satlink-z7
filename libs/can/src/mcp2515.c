/**
 * @file mcp2515.c
 * @brief MCP2515 driver: SPI instruction set, bit timing, frame encoding.
 *
 * @implements SRS-HKC-002
 */
#include "satlink/can/mcp2515.h"

/* SPI instructions. */
#define MCP_INSTR_RESET       (0xC0U)
#define MCP_INSTR_READ        (0x03U)
#define MCP_INSTR_WRITE       (0x02U)
#define MCP_INSTR_READ_STATUS (0xA0U)
#define MCP_INSTR_BIT_MODIFY  (0x05U)
#define MCP_INSTR_READ_RXB0   (0x90U)
#define MCP_INSTR_READ_RXB1   (0x94U)
#define MCP_INSTR_LOAD_TXB0   (0x40U)
#define MCP_INSTR_RTS_TXB0    (0x81U)

/* Registers. */
#define MCP_REG_CANSTAT  (0x0EU)
#define MCP_REG_CANCTRL  (0x0FU)
#define MCP_REG_CNF3     (0x28U)
#define MCP_REG_CANINTE  (0x2BU)
#define MCP_REG_EFLG     (0x2DU)
#define MCP_REG_TXB0CTRL (0x30U)
#define MCP_REG_RXB0CTRL (0x60U)
#define MCP_REG_RXB1CTRL (0x70U)

#define MCP_OPMOD_SHIFT  (5U)
#define MCP_OPMOD_MASK   (0xE0U)
#define MCP_TXREQ        (0x08U)
#define MCP_RXM_ANY      (0x60U)
#define MCP_RXB0_BUKT    (0x04U)
#define MCP_STATUS_RX0IF (0x01U)
#define MCP_STATUS_RX1IF (0x02U)

/* SIDL bits. */
#define MCP_SIDL_EXIDE (0x08U)
#define MCP_SIDL_SRR   (0x10U)
#define MCP_DLC_RTR    (0x40U)

#define MCP_RESET_DELAY_US     (1000U)
#define MCP_MODE_POLL_TRIES    (20U)
#define MCP_MODE_POLL_DELAY_US (100U)

/* Bit timing limits. */
#define MCP_TQ_PER_BIT_MIN (8U)
#define MCP_SEG_MAX        (16U)
#define MCP_BRP_MAX        (64U)
#define MCP_BITRATE_MAX    (1000000U)
#define MCP_OSC_MAX        (100000000U)

/** Time quanta per bit to try, best first (16 gives the customary 87.5 % sample point). */
static const uint8_t k_tq_order[] = {16U, 15U, 14U, 13U, 12U, 11U, 10U, 9U, 8U, 17U, 18U, 19U};

static satlink_status_t mcp_transfer(const satlink_mcp2515_t *dev, const uint8_t *tx, uint8_t *rx,
                                     size_t len)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((dev != NULL) && (dev->transfer != NULL))
    {
        status = dev->transfer(dev->ctx, tx, rx, len);
    }

    return status;
}

static void mcp_delay(const satlink_mcp2515_t *dev, uint32_t microseconds)
{
    if ((dev != NULL) && (dev->delay_us != NULL))
    {
        dev->delay_us(dev->ctx, microseconds);
    }
}

static satlink_status_t mcp_read_reg(const satlink_mcp2515_t *dev, uint8_t reg, uint8_t *value)
{
    const uint8_t tx[3] = {(uint8_t)MCP_INSTR_READ, reg, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};
    const satlink_status_t status = mcp_transfer(dev, tx, rx, sizeof(tx));

    if (status == SATLINK_OK)
    {
        *value = rx[2];
    }

    return status;
}

static satlink_status_t mcp_write_reg(const satlink_mcp2515_t *dev, uint8_t reg, uint8_t value)
{
    const uint8_t tx[3] = {(uint8_t)MCP_INSTR_WRITE, reg, value};
    uint8_t rx[3] = {0U, 0U, 0U};

    return mcp_transfer(dev, tx, rx, sizeof(tx));
}

static satlink_status_t mcp_bit_modify(const satlink_mcp2515_t *dev, uint8_t reg, uint8_t mask,
                                       uint8_t value)
{
    const uint8_t tx[4] = {(uint8_t)MCP_INSTR_BIT_MODIFY, reg, mask, value};
    uint8_t rx[4] = {0U, 0U, 0U, 0U};

    return mcp_transfer(dev, tx, rx, sizeof(tx));
}

satlink_status_t satlink_mcp2515_calc_timing(uint32_t osc_hz, uint32_t bitrate,
                                             satlink_mcp2515_timing_t *timing)
{
    satlink_status_t status = SATLINK_ERR_RANGE;

    if (timing == NULL)
    {
        status = SATLINK_ERR_NULL;
    }
    else if ((osc_hz != 0U) && (osc_hz <= MCP_OSC_MAX) && (bitrate != 0U) &&
             (bitrate <= MCP_BITRATE_MAX))
    {
        for (size_t i = 0U; (i < sizeof(k_tq_order)) && (status != SATLINK_OK); ++i)
        {
            const uint32_t tq = (uint32_t)k_tq_order[i];
            const uint32_t divisor = 2U * bitrate * tq;

            if ((osc_hz % divisor) == 0U)
            {
                const uint32_t brp = osc_hz / divisor;
                /* Phase segment 2 is at least two quanta; the rest is split between phase
                 * segment 1 and the propagation segment. */
                const uint32_t ps2 = ((tq / 8U) > 2U) ? (tq / 8U) : 2U;
                const uint32_t seg = tq - 1U - ps2;
                const uint32_t ps1 = (seg + 1U) / 2U;
                const uint32_t prop = seg - ps1;

                if ((brp >= 1U) && (brp <= MCP_BRP_MAX) && (seg >= 2U) && (seg <= MCP_SEG_MAX) &&
                    (tq >= MCP_TQ_PER_BIT_MIN))
                {
                    timing->cnf1 = (uint8_t)(brp - 1U); /* SJW = 1 quantum */
                    timing->cnf2 = (uint8_t)(0x80U | ((ps1 - 1U) << 3U) | (prop - 1U));
                    timing->cnf3 = (uint8_t)(ps2 - 1U);
                    status = SATLINK_OK;
                }
            }
        }
    }

    return status;
}

satlink_status_t satlink_mcp2515_init(satlink_mcp2515_t *dev, satlink_mcp2515_transfer_fn transfer,
                                      satlink_mcp2515_delay_fn delay_us, void *ctx)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((dev != NULL) && (transfer != NULL))
    {
        dev->transfer = transfer;
        dev->delay_us = delay_us;
        dev->ctx = ctx;
        status = SATLINK_OK;
    }

    return status;
}

satlink_status_t satlink_mcp2515_set_mode(const satlink_mcp2515_t *dev, satlink_mcp2515_mode_t mode)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (dev != NULL)
    {
        const uint8_t want = (uint8_t)((uint32_t)mode << MCP_OPMOD_SHIFT);

        if (((uint32_t)mode) > (uint32_t)SATLINK_MCP2515_MODE_CONFIG)
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            status = mcp_bit_modify(dev, (uint8_t)MCP_REG_CANCTRL, (uint8_t)MCP_OPMOD_MASK, want);

            /* The controller changes mode when the bus is idle, so poll CANSTAT. */
            for (uint32_t tries = 0U; (status == SATLINK_OK) && (tries < MCP_MODE_POLL_TRIES);
                 ++tries)
            {
                uint8_t canstat = 0U;

                status = mcp_read_reg(dev, (uint8_t)MCP_REG_CANSTAT, &canstat);
                if ((status == SATLINK_OK) && ((canstat & MCP_OPMOD_MASK) == want))
                {
                    break;
                }
                if ((status == SATLINK_OK) && (tries == (MCP_MODE_POLL_TRIES - 1U)))
                {
                    status = SATLINK_ERR_TIMEOUT;
                }
                mcp_delay(dev, MCP_MODE_POLL_DELAY_US);
            }
        }
    }

    return status;
}

satlink_status_t satlink_mcp2515_configure(const satlink_mcp2515_t *dev, uint32_t osc_hz,
                                           uint32_t bitrate, satlink_mcp2515_mode_t mode)
{
    satlink_mcp2515_timing_t timing = {0U, 0U, 0U};
    satlink_status_t status = satlink_mcp2515_calc_timing(osc_hz, bitrate, &timing);

    if ((status == SATLINK_OK) && (dev == NULL))
    {
        status = SATLINK_ERR_NULL;
    }

    if (status == SATLINK_OK)
    {
        const uint8_t reset[1] = {(uint8_t)MCP_INSTR_RESET};
        uint8_t dummy[1] = {0U};

        status = mcp_transfer(dev, reset, dummy, sizeof(reset));
        mcp_delay(dev, MCP_RESET_DELAY_US);
    }

    if (status == SATLINK_OK)
    {
        uint8_t canstat = 0U;

        status = mcp_read_reg(dev, (uint8_t)MCP_REG_CANSTAT, &canstat);
        if ((status == SATLINK_OK) && ((canstat & MCP_OPMOD_MASK) !=
                                       ((uint32_t)SATLINK_MCP2515_MODE_CONFIG << MCP_OPMOD_SHIFT)))
        {
            status = SATLINK_ERR_IO; /* no controller answering, or it did not reset */
        }
    }

    if (status == SATLINK_OK)
    {
        /* CNF3, CNF2 and CNF1 are consecutive registers, so one WRITE sets all three. */
        const uint8_t tx[5] = {(uint8_t)MCP_INSTR_WRITE, (uint8_t)MCP_REG_CNF3, timing.cnf3,
                               timing.cnf2, timing.cnf1};
        uint8_t rx[5] = {0U, 0U, 0U, 0U, 0U};

        status = mcp_transfer(dev, tx, rx, sizeof(tx));
    }

    if (status == SATLINK_OK)
    {
        status = mcp_write_reg(dev, (uint8_t)MCP_REG_CANINTE, 0U);
    }
    if (status == SATLINK_OK)
    {
        status =
            mcp_write_reg(dev, (uint8_t)MCP_REG_RXB0CTRL, (uint8_t)(MCP_RXM_ANY | MCP_RXB0_BUKT));
    }
    if (status == SATLINK_OK)
    {
        status = mcp_write_reg(dev, (uint8_t)MCP_REG_RXB1CTRL, (uint8_t)MCP_RXM_ANY);
    }
    if (status == SATLINK_OK)
    {
        status = satlink_mcp2515_set_mode(dev, mode);
    }

    return status;
}

/** Write the five identifier/length bytes for @p frame into @p out (SIDH, SIDL, EID8, EID0, DLC).
 */
static void mcp_encode_header(const satlink_can_frame_t *frame, uint8_t *out)
{
    if (frame->extended)
    {
        const uint32_t id = frame->id;

        out[0] = (uint8_t)(id >> 21U);
        out[1] = (uint8_t)((((id >> 18U) & 0x07U) << 5U) | MCP_SIDL_EXIDE | ((id >> 16U) & 0x03U));
        out[2] = (uint8_t)((id >> 8U) & 0xFFU);
        out[3] = (uint8_t)(id & 0xFFU);
    }
    else
    {
        out[0] = (uint8_t)(frame->id >> 3U);
        out[1] = (uint8_t)((frame->id & 0x07U) << 5U);
        out[2] = 0U;
        out[3] = 0U;
    }

    out[4] = (uint8_t)((frame->rtr ? MCP_DLC_RTR : 0U) | (uint32_t)frame->dlc);
}

/** Decode a 14-byte RX buffer read (instruction echo, SIDH, SIDL, EID8, EID0, DLC, D0..D7). */
static void mcp_decode_frame(const uint8_t *rx, satlink_can_frame_t *frame)
{
    const uint8_t sidl = rx[2];
    const uint32_t std_id = ((uint32_t)rx[1] << 3U) | ((uint32_t)sidl >> 5U);
    const uint8_t dlc_reg = rx[5];
    uint8_t dlc = (uint8_t)(dlc_reg & 0x0FU);

    frame->extended = ((sidl & MCP_SIDL_EXIDE) != 0U);
    if (frame->extended)
    {
        frame->id = (std_id << 18U) | (((uint32_t)sidl & 0x03U) << 16U) | ((uint32_t)rx[3] << 8U) |
                    (uint32_t)rx[4];
        frame->rtr = ((dlc_reg & MCP_DLC_RTR) != 0U);
    }
    else
    {
        frame->id = std_id;
        frame->rtr = ((sidl & MCP_SIDL_SRR) != 0U);
    }

    if (dlc > SATLINK_CAN_MAX_DLC)
    {
        dlc = (uint8_t)SATLINK_CAN_MAX_DLC;
    }
    frame->dlc = dlc;
    for (size_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = (i < (size_t)dlc) ? rx[6U + i] : 0U;
    }
}

satlink_status_t satlink_mcp2515_send(const satlink_mcp2515_t *dev,
                                      const satlink_can_frame_t *frame)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((dev != NULL) && (frame != NULL))
    {
        const uint32_t id_max = frame->extended ? SATLINK_CAN_EXT_ID_MAX : SATLINK_CAN_STD_ID_MAX;

        if ((frame->dlc > SATLINK_CAN_MAX_DLC) || (frame->id > id_max))
        {
            status = SATLINK_ERR_RANGE;
        }
        else
        {
            uint8_t ctrl = 0U;

            status = mcp_read_reg(dev, (uint8_t)MCP_REG_TXB0CTRL, &ctrl);
            if ((status == SATLINK_OK) && ((ctrl & MCP_TXREQ) != 0U))
            {
                status = SATLINK_ERR_BUSY;
            }

            if (status == SATLINK_OK)
            {
                uint8_t tx[1U + 5U + SATLINK_CAN_MAX_DLC] = {0U};
                uint8_t rx[1U + 5U + SATLINK_CAN_MAX_DLC] = {0U};
                const size_t len = 1U + 5U + (size_t)frame->dlc;

                tx[0] = (uint8_t)MCP_INSTR_LOAD_TXB0;
                mcp_encode_header(frame, &tx[1]);
                for (size_t i = 0U; i < (size_t)frame->dlc; ++i)
                {
                    tx[6U + i] = frame->data[i];
                }
                status = mcp_transfer(dev, tx, rx, len);
            }

            if (status == SATLINK_OK)
            {
                const uint8_t rts[1] = {(uint8_t)MCP_INSTR_RTS_TXB0};
                uint8_t dummy[1] = {0U};

                status = mcp_transfer(dev, rts, dummy, sizeof(rts));
            }
        }
    }

    return status;
}

satlink_status_t satlink_mcp2515_receive(const satlink_mcp2515_t *dev, satlink_can_frame_t *frame)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((dev != NULL) && (frame != NULL))
    {
        const uint8_t status_tx[2] = {(uint8_t)MCP_INSTR_READ_STATUS, 0U};
        uint8_t status_rx[2] = {0U, 0U};

        status = mcp_transfer(dev, status_tx, status_rx, sizeof(status_tx));

        if (status == SATLINK_OK)
        {
            const uint8_t flags = status_rx[1];

            if ((flags & (MCP_STATUS_RX0IF | MCP_STATUS_RX1IF)) == 0U)
            {
                status = SATLINK_ERR_EMPTY;
            }
            else
            {
                /* Reading through the RX buffer instruction also clears the RXnIF flag. */
                const uint8_t instr = ((flags & MCP_STATUS_RX0IF) != 0U)
                                          ? (uint8_t)MCP_INSTR_READ_RXB0
                                          : (uint8_t)MCP_INSTR_READ_RXB1;
                uint8_t tx[1U + 5U + SATLINK_CAN_MAX_DLC] = {0U};
                uint8_t rx[1U + 5U + SATLINK_CAN_MAX_DLC] = {0U};

                tx[0] = instr;
                status = mcp_transfer(dev, tx, rx, sizeof(tx));

                if (status == SATLINK_OK)
                {
                    mcp_decode_frame(rx, frame);
                }
            }
        }
    }

    return status;
}

satlink_status_t satlink_mcp2515_error_flags(const satlink_mcp2515_t *dev, uint8_t *flags)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if ((dev != NULL) && (flags != NULL))
    {
        status = mcp_read_reg(dev, (uint8_t)MCP_REG_EFLG, flags);
    }

    return status;
}

satlink_status_t satlink_mcp2515_clear_overflow(const satlink_mcp2515_t *dev)
{
    satlink_status_t status = SATLINK_ERR_NULL;

    if (dev != NULL)
    {
        status = mcp_bit_modify(
            dev, (uint8_t)MCP_REG_EFLG,
            (uint8_t)(SATLINK_MCP2515_EFLG_RX0OVR | SATLINK_MCP2515_EFLG_RX1OVR), 0U);
    }

    return status;
}
