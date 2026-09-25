/**
 * @file mcp2515.c
 * @implements SRS-HKC-001
 */
#include "satlink/drivers/mcp2515.h"

/* Polls of CANSTAT before a mode change is declared failed. Each poll is one SPI transfer
 * (several microseconds), far longer than the 128 oscillator cycles the chip needs. */
#define MODE_POLL_LIMIT (1000U)

/* READ STATUS bits. */
#define STATUS_RX0IF (0x01U)
#define STATUS_RX1IF (0x02U)

#define SIDL_IDE (0x08U)

#define RX_BUFFER_BYTES (13U) /* SIDH SIDL EID8 EID0 DLC D0..D7 */

static satlink_status_t xfer(satlink_mcp2515_t *dev, const uint8_t *tx, uint8_t *rx, size_t len)
{
    return (dev->xfer(dev->ctx, tx, rx, len) == 0) ? SATLINK_OK : SATLINK_ERR_IO;
}

satlink_status_t satlink_mcp2515_read_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t *value)
{
    if ((dev == NULL) || (value == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    const uint8_t tx[3] = {MCP2515_SPI_READ, reg, 0U};
    uint8_t rx[3] = {0U, 0U, 0U};
    const satlink_status_t status = xfer(dev, tx, rx, sizeof(tx));
    *value = rx[2];
    return status;
}

satlink_status_t satlink_mcp2515_write_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t value)
{
    if (dev == NULL)
    {
        return SATLINK_ERR_NULL;
    }
    const uint8_t tx[3] = {MCP2515_SPI_WRITE, reg, value};
    return xfer(dev, tx, NULL, sizeof(tx));
}

satlink_status_t satlink_mcp2515_modify_reg(satlink_mcp2515_t *dev, uint8_t reg, uint8_t mask,
                                            uint8_t value)
{
    if (dev == NULL)
    {
        return SATLINK_ERR_NULL;
    }
    const uint8_t tx[4] = {MCP2515_SPI_BIT_MODIFY, reg, mask, value};
    return xfer(dev, tx, NULL, sizeof(tx));
}

satlink_status_t satlink_mcp2515_bit_timing(uint32_t fosc_hz, uint32_t bitrate,
                                            satlink_mcp2515_timing_t *timing)
{
    if (timing == NULL)
    {
        return SATLINK_ERR_NULL;
    }
    if ((fosc_hz == 0U) || (bitrate == 0U))
    {
        return SATLINK_ERR_RANGE;
    }

    /* Try 16 TQ first (the usual choice), then the others from the largest down. */
    static const uint8_t k_order[] = {16U, 25U, 24U, 23U, 22U, 21U, 20U, 19U, 18U,
                                      17U, 15U, 14U, 13U, 12U, 11U, 10U, 9U,  8U};
    for (uint32_t i = 0U; i < (uint32_t)sizeof(k_order); ++i)
    {
        const uint32_t ntq = k_order[i];
        const uint64_t divisor = 2ULL * (uint64_t)ntq * (uint64_t)bitrate;
        if (((uint64_t)fosc_hz % divisor) != 0U)
        {
            continue;
        }
        const uint64_t brp = (uint64_t)fosc_hz / divisor;
        if ((brp < 1U) || (brp > 64U))
        {
            continue;
        }
        /* Sample point near 75 %: PS2 = round(ntq / 4), at least 2. */
        uint32_t ps2 = (ntq + 2U) / 4U;
        if (ps2 < 2U)
        {
            ps2 = 2U;
        }
        const uint32_t rest = ntq - 1U - ps2; /* PropSeg + PS1 */
        uint32_t prop = rest / 2U;
        if (prop > 8U)
        {
            prop = 8U;
        }
        const uint32_t ps1 = rest - prop;
        if ((ps1 < 1U) || (ps1 > 8U) || (prop < 1U) || (ps2 > 8U))
        {
            continue;
        }
        timing->cnf1 = (uint8_t)(brp - 1U);                                 /* SJW = 1 */
        timing->cnf2 = (uint8_t)(0x80U | ((ps1 - 1U) << 3U) | (prop - 1U)); /* BTLMODE = 1 */
        timing->cnf3 = (uint8_t)(ps2 - 1U);
        return SATLINK_OK;
    }
    return SATLINK_ERR_RANGE;
}

static satlink_status_t request_mode(satlink_mcp2515_t *dev, uint8_t mode)
{
    satlink_status_t status =
        satlink_mcp2515_modify_reg(dev, MCP2515_REG_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mode);
    for (uint32_t i = 0U; (status == SATLINK_OK) && (i < MODE_POLL_LIMIT); ++i)
    {
        uint8_t canstat = 0U;
        status = satlink_mcp2515_read_reg(dev, MCP2515_REG_CANSTAT, &canstat);
        if ((status == SATLINK_OK) && ((canstat & MCP2515_CANCTRL_REQOP_MASK) == mode))
        {
            return SATLINK_OK;
        }
    }
    return (status == SATLINK_OK) ? SATLINK_ERR_TIMEOUT : status;
}

static satlink_status_t wait_config_mode(satlink_mcp2515_t *dev)
{
    for (uint32_t i = 0U; i < MODE_POLL_LIMIT; ++i)
    {
        uint8_t canstat = 0U;
        const satlink_status_t status =
            satlink_mcp2515_read_reg(dev, MCP2515_REG_CANSTAT, &canstat);
        if (status != SATLINK_OK)
        {
            return status;
        }
        if ((canstat & MCP2515_CANCTRL_REQOP_MASK) == MCP2515_MODE_CONFIG)
        {
            return SATLINK_OK;
        }
    }
    return SATLINK_ERR_TIMEOUT;
}

satlink_status_t satlink_mcp2515_init(satlink_mcp2515_t *dev, satlink_mcp2515_xfer_fn xfer_fn,
                                      void *ctx, const satlink_mcp2515_timing_t *timing,
                                      satlink_mcp2515_mode_t mode)
{
    if ((dev == NULL) || (xfer_fn == NULL) || (timing == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    dev->xfer = xfer_fn;
    dev->ctx = ctx;
    dev->rx_overruns = 0U;

    const uint8_t reset = MCP2515_SPI_RESET;
    satlink_status_t status = xfer(dev, &reset, NULL, 1U);
    if (status == SATLINK_OK)
    {
        status = wait_config_mode(dev);
    }
    if (status == SATLINK_OK)
    {
        /* CNF3, CNF2, CNF1, CANINTE are consecutive: one sequential write. */
        const uint8_t tx[6] = {MCP2515_SPI_WRITE, MCP2515_REG_CNF3, timing->cnf3,
                               timing->cnf2,      timing->cnf1,     0U};
        status = xfer(dev, tx, NULL, sizeof(tx));
    }
    if (status == SATLINK_OK)
    {
        status = satlink_mcp2515_write_reg(dev, MCP2515_REG_RXB0CTRL,
                                           (uint8_t)(MCP2515_RXBCTRL_ANY | MCP2515_RXB0CTRL_BUKT));
    }
    if (status == SATLINK_OK)
    {
        status = satlink_mcp2515_write_reg(dev, MCP2515_REG_RXB1CTRL, MCP2515_RXBCTRL_ANY);
    }
    if (status == SATLINK_OK)
    {
        status = satlink_mcp2515_write_reg(dev, MCP2515_REG_CANINTF, 0U);
    }
    if (status == SATLINK_OK)
    {
        status = satlink_mcp2515_write_reg(
            dev, MCP2515_REG_CANINTE,
            (uint8_t)(MCP2515_CANINT_RX0I | MCP2515_CANINT_RX1I | MCP2515_CANINT_ERRI));
    }
    if (status == SATLINK_OK)
    {
        status = request_mode(dev, (mode == SATLINK_MCP2515_LOOPBACK) ? MCP2515_MODE_LOOPBACK
                                                                      : MCP2515_MODE_NORMAL);
    }
    return status;
}

satlink_status_t satlink_mcp2515_send(satlink_mcp2515_t *dev, const satlink_can_frame_t *frame)
{
    if ((dev == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    if ((frame->id > SATLINK_CAN_MAX_STD_ID) || (frame->dlc > SATLINK_CAN_MAX_DLC))
    {
        return SATLINK_ERR_RANGE;
    }
    uint8_t ctrl = 0U;
    satlink_status_t status = satlink_mcp2515_read_reg(dev, MCP2515_REG_TXB0CTRL, &ctrl);
    if (status != SATLINK_OK)
    {
        return status;
    }
    if ((ctrl & MCP2515_TXBCTRL_TXREQ) != 0U)
    {
        return SATLINK_ERR_FULL;
    }

    uint8_t tx[14];
    tx[0] = MCP2515_SPI_LOAD_TXB0;
    tx[1] = (uint8_t)(frame->id >> 3U);
    tx[2] = (uint8_t)((frame->id & 0x07U) << 5U);
    tx[3] = 0U;
    tx[4] = 0U;
    tx[5] = frame->dlc;
    for (uint32_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        tx[6U + i] = (i < (uint32_t)frame->dlc) ? frame->data[i] : 0U;
    }
    status = xfer(dev, tx, NULL, 6U + (size_t)frame->dlc);
    if (status == SATLINK_OK)
    {
        const uint8_t rts = MCP2515_SPI_RTS_TXB0;
        status = xfer(dev, &rts, NULL, 1U);
    }
    return status;
}

static satlink_status_t check_overrun(satlink_mcp2515_t *dev)
{
    uint8_t eflg = 0U;
    satlink_status_t status = satlink_mcp2515_read_reg(dev, MCP2515_REG_EFLG, &eflg);
    const uint8_t ovr = (uint8_t)(eflg & (MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR));
    if ((status == SATLINK_OK) && (ovr != 0U))
    {
        dev->rx_overruns += ((ovr & MCP2515_EFLG_RX0OVR) != 0U) ? 1U : 0U;
        dev->rx_overruns += ((ovr & MCP2515_EFLG_RX1OVR) != 0U) ? 1U : 0U;
        status = satlink_mcp2515_modify_reg(dev, MCP2515_REG_EFLG, ovr, 0U);
    }
    return status;
}

satlink_status_t satlink_mcp2515_receive(satlink_mcp2515_t *dev, satlink_can_frame_t *frame)
{
    if ((dev == NULL) || (frame == NULL))
    {
        return SATLINK_ERR_NULL;
    }
    satlink_status_t status = check_overrun(dev);
    if (status != SATLINK_OK)
    {
        return status;
    }

    const uint8_t tx_status[2] = {MCP2515_SPI_READ_STATUS, 0U};
    uint8_t rx_status[2] = {0U, 0U};
    status = xfer(dev, tx_status, rx_status, sizeof(tx_status));
    if (status != SATLINK_OK)
    {
        return status;
    }

    uint8_t instruction = 0U;
    if ((rx_status[1] & STATUS_RX0IF) != 0U)
    {
        instruction = MCP2515_SPI_READ_RXB0; /* RXB0 fills first, so it holds the older frame */
    }
    else if ((rx_status[1] & STATUS_RX1IF) != 0U)
    {
        instruction = MCP2515_SPI_READ_RXB1;
    }
    else
    {
        return SATLINK_ERR_EMPTY;
    }

    uint8_t tx[1U + RX_BUFFER_BYTES] = {0U};
    uint8_t rx[1U + RX_BUFFER_BYTES] = {0U};
    tx[0] = instruction;
    status = xfer(dev, tx, rx, sizeof(tx)); /* the flag clears when CS rises */
    if (status != SATLINK_OK)
    {
        return status;
    }

    const uint8_t sidh = rx[1];
    const uint8_t sidl = rx[2];
    if ((sidl & SIDL_IDE) != 0U)
    {
        return SATLINK_ERR_RANGE; /* extended frame: not used on the SatLink bus, dropped */
    }
    frame->id = (uint16_t)(((uint32_t)sidh << 3U) | ((uint32_t)sidl >> 5U));
    uint8_t dlc = (uint8_t)(rx[5] & 0x0FU);
    if (dlc > SATLINK_CAN_MAX_DLC)
    {
        dlc = SATLINK_CAN_MAX_DLC;
    }
    frame->dlc = dlc;
    for (uint32_t i = 0U; i < SATLINK_CAN_MAX_DLC; ++i)
    {
        frame->data[i] = (i < (uint32_t)dlc) ? rx[6U + i] : 0U;
    }
    return SATLINK_OK;
}
