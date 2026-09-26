/**
 * @file test_mcp2515.cpp
 * @brief MCP2515 driver against a register-level model of the chip's SPI interface.
 *
 * The model decodes the SPI instructions the way the datasheet describes them (sequential
 * READ/WRITE, BIT MODIFY, LOAD TX BUFFER, RTS, READ RX BUFFER clearing the RX flag, READ
 * STATUS) and implements loopback mode, RXB0 -> RXB1 rollover and receive overruns.
 *
 * @verifies SRS-HKC-001
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/drivers/mcp2515.h"

namespace satlink::test {
namespace {

class Mcp2515Model
{
  public:
    Mcp2515Model()
    {
        Reset();
    }

    static int Xfer(void *ctx, const std::uint8_t *tx, std::uint8_t *rx, std::size_t len)
    {
        return static_cast<Mcp2515Model *>(ctx)->Transfer(tx, rx, len);
    }

    /// A frame arriving from the bus (normal mode).
    void Inject(std::uint16_t id, std::vector<std::uint8_t> data)
    {
        std::array<std::uint8_t, 13> buf{};
        buf[0] = static_cast<std::uint8_t>(id >> 3U);
        buf[1] = static_cast<std::uint8_t>((id & 7U) << 5U);
        buf[4] = static_cast<std::uint8_t>(data.size());
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            buf[5 + i] = data[i];
        }
        Receive(buf);
    }

    std::array<std::uint8_t, 128> regs{};
    std::vector<std::array<std::uint8_t, 13>> transmitted; ///< Frames sent to the bus.
    int transfers = 0;
    int fail_at = -1; ///< Transfer number that fails (-1: never).
    bool stuck_in_config = false;

  private:
    static constexpr std::uint8_t kCanstat = 0x0E;
    static constexpr std::uint8_t kCanctrl = 0x0F;
    static constexpr std::uint8_t kCanintf = 0x2C;
    static constexpr std::uint8_t kEflg = 0x2D;
    static constexpr std::uint8_t kTxb0ctrl = 0x30;

    void Reset()
    {
        regs.fill(0);
        regs[kCanstat] = 0x80;
        regs[kCanctrl] = 0x87;
    }

    void WriteReg(std::uint8_t reg, std::uint8_t value)
    {
        regs[reg & 0x7FU] = value;
        if (reg == kCanctrl && !stuck_in_config)
        {
            regs[kCanstat] = static_cast<std::uint8_t>((regs[kCanstat] & 0x1FU) | (value & 0xE0U));
        }
    }

    void Receive(const std::array<std::uint8_t, 13> &buf)
    {
        std::uint8_t base = 0;
        if ((regs[kCanintf] & 0x01U) == 0U)
        {
            base = 0x61;
            regs[kCanintf] |= 0x01U;
        }
        else if ((regs[0x60] & 0x04U) != 0U && (regs[kCanintf] & 0x02U) == 0U)
        {
            base = 0x71; // rollover
            regs[kCanintf] |= 0x02U;
        }
        else
        {
            regs[kEflg] = static_cast<std::uint8_t>(regs[kEflg] |
                                                    (((regs[0x60] & 0x04U) != 0U) ? 0x80U : 0x40U));
            return;
        }
        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            regs[base + i] = buf[i];
        }
    }

    int Transfer(const std::uint8_t *tx, std::uint8_t *rx, std::size_t len)
    {
        if (transfers++ == fail_at)
        {
            return -1;
        }
        std::vector<std::uint8_t> out(len, 0);
        switch (tx[0])
        {
        case 0xC0:
            Reset();
            break;
        case 0x03:
            for (std::size_t i = 2; i < len; ++i)
            {
                out[i] = regs[(tx[1] + i - 2) & 0x7FU];
            }
            break;
        case 0x02:
            for (std::size_t i = 2; i < len; ++i)
            {
                WriteReg(static_cast<std::uint8_t>(tx[1] + i - 2), tx[i]);
            }
            break;
        case 0x05:
        {
            const std::uint8_t reg = tx[1];
            WriteReg(reg, static_cast<std::uint8_t>((regs[reg] & ~tx[2]) | (tx[3] & tx[2])));
            break;
        }
        case 0xA0:
        {
            const std::uint8_t s = static_cast<std::uint8_t>(
                (regs[kCanintf] & 0x03U) | ((regs[kTxb0ctrl] & 0x08U) ? 0x04U : 0U));
            for (std::size_t i = 1; i < len; ++i)
            {
                out[i] = s;
            }
            break;
        }
        case 0x40:
            for (std::size_t i = 1; i < len; ++i)
            {
                regs[0x31 + i - 1] = tx[i];
            }
            break;
        case 0x81:
        {
            std::array<std::uint8_t, 13> buf{};
            for (std::size_t i = 0; i < buf.size(); ++i)
            {
                buf[i] = regs[0x31 + i];
            }
            if ((regs[kCanstat] & 0xE0U) == 0x40U)
            {
                Receive(buf); // loopback
            }
            else
            {
                transmitted.push_back(buf);
            }
            break;
        }
        case 0x90:
        case 0x94:
        {
            const std::uint8_t base = (tx[0] == 0x90) ? 0x61 : 0x71;
            for (std::size_t i = 1; i < len; ++i)
            {
                out[i] = regs[base + i - 1];
            }
            regs[kCanintf] &= static_cast<std::uint8_t>(tx[0] == 0x90 ? ~0x01U : ~0x02U);
            break;
        }
        default:
            ADD_FAILURE() << "unexpected SPI instruction 0x" << std::hex << int{tx[0]};
        }
        if (rx != nullptr)
        {
            for (std::size_t i = 0; i < len; ++i)
            {
                rx[i] = out[i];
            }
        }
        return 0;
    }
};

class Mcp2515Test : public ::testing::Test
{
  protected:
    satlink_status_t Init(satlink_mcp2515_mode_t mode)
    {
        satlink_mcp2515_timing_t timing{};
        EXPECT_EQ(SATLINK_OK, satlink_mcp2515_bit_timing(16000000U, 500000U, &timing));
        return satlink_mcp2515_init(&dev_, &Mcp2515Model::Xfer, &chip_, &timing, mode);
    }

    static satlink_can_frame_t Frame(std::uint16_t id, std::vector<std::uint8_t> data)
    {
        satlink_can_frame_t f{};
        f.id = id;
        f.dlc = static_cast<std::uint8_t>(data.size());
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            f.data[i] = data[i];
        }
        return f;
    }

    Mcp2515Model chip_;
    satlink_mcp2515_t dev_{};
};

TEST(Mcp2515TimingTest, PmodCan500kbitAt16MHz)
{
    satlink_mcp2515_timing_t t{};
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_bit_timing(16000000U, 500000U, &t));
    // BRP = 1 (cnf1 = 0), 16 TQ: sync 1 + prop 5 + PS1 6 + PS2 4, sample point 75 %.
    EXPECT_EQ(0x00, t.cnf1);
    EXPECT_EQ(0x80 | (5 << 3) | 4, t.cnf2);
    EXPECT_EQ(0x03, t.cnf3);
}

TEST(Mcp2515TimingTest, OtherRatesAndImpossibleOnes)
{
    satlink_mcp2515_timing_t t{};
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_bit_timing(16000000U, 125000U, &t));
    EXPECT_EQ(3, t.cnf1); // BRP = 4
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_bit_timing(16000000U, 1000000U, &t));
    EXPECT_EQ(0, t.cnf1); // 8 TQ
    EXPECT_EQ(1, t.cnf3); // PS2 = 2
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_mcp2515_bit_timing(16000000U, 3000000U, &t));
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_mcp2515_bit_timing(16000000U, 0U, &t));
    EXPECT_EQ(SATLINK_ERR_NULL, satlink_mcp2515_bit_timing(16000000U, 500000U, nullptr));
}

TEST_F(Mcp2515Test, InitProgramsTimingFiltersInterruptsAndMode)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_NORMAL));
    EXPECT_EQ(0x03, chip_.regs[0x28]);
    EXPECT_EQ(0xAC, chip_.regs[0x29]);
    EXPECT_EQ(0x00, chip_.regs[0x2A]);
    EXPECT_EQ(0x64, chip_.regs[0x60]); // receive any, rollover
    EXPECT_EQ(0x60, chip_.regs[0x70]);
    EXPECT_EQ(0x23, chip_.regs[0x2B]); // RX0IE | RX1IE | ERRIE
    EXPECT_EQ(0x00, chip_.regs[0x0E] & 0xE0);
}

TEST_F(Mcp2515Test, InitTimesOutWhenModeNeverChanges)
{
    chip_.stuck_in_config = true;
    EXPECT_EQ(SATLINK_ERR_TIMEOUT, Init(SATLINK_MCP2515_NORMAL));
}

TEST_F(Mcp2515Test, InitReportsSpiFailure)
{
    chip_.fail_at = 3;
    EXPECT_EQ(SATLINK_ERR_IO, Init(SATLINK_MCP2515_NORMAL));
}

TEST_F(Mcp2515Test, LoopbackRoundTrip)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_LOOPBACK));
    satlink_can_frame_t rx{};
    EXPECT_EQ(SATLINK_ERR_EMPTY, satlink_mcp2515_receive(&dev_, &rx));

    const auto tx = Frame(0x7E8, {0x81, 0x00, 1, 4});
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_send(&dev_, &tx));
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_receive(&dev_, &rx));
    EXPECT_EQ(0x7E8, rx.id);
    ASSERT_EQ(4, rx.dlc);
    EXPECT_EQ(0x81, rx.data[0]);
    EXPECT_EQ(4, rx.data[3]);
    EXPECT_EQ(0, rx.data[4]);
    EXPECT_EQ(SATLINK_ERR_EMPTY, satlink_mcp2515_receive(&dev_, &rx));
}

TEST_F(Mcp2515Test, SendEncodesIdAndRejectsBadFrames)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_NORMAL));
    const auto tx = Frame(0x102, {1, 2, 3});
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_send(&dev_, &tx));
    ASSERT_EQ(1U, chip_.transmitted.size());
    EXPECT_EQ(0x20, chip_.transmitted[0][0]); // 0x102 >> 3
    EXPECT_EQ(0x40, chip_.transmitted[0][1]); // (0x102 & 7) << 5
    EXPECT_EQ(3, chip_.transmitted[0][4]);

    auto bad = Frame(0x800, {});
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_mcp2515_send(&dev_, &bad));
    bad = Frame(0x100, {});
    bad.dlc = 9;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_mcp2515_send(&dev_, &bad));
}

TEST_F(Mcp2515Test, SendReportsFullWhilePending)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_NORMAL));
    chip_.regs[0x30] = 0x08; // TXREQ still set
    const auto tx = Frame(0x100, {1});
    EXPECT_EQ(SATLINK_ERR_FULL, satlink_mcp2515_send(&dev_, &tx));
}

TEST_F(Mcp2515Test, RolloverKeepsOrderAndOverrunIsCounted)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_NORMAL));
    chip_.Inject(0x080, {1});
    chip_.Inject(0x180, {2});
    chip_.Inject(0x181, {3}); // both buffers full: dropped

    satlink_can_frame_t rx{};
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_receive(&dev_, &rx));
    EXPECT_EQ(0x080, rx.id);
    EXPECT_EQ(1U, dev_.rx_overruns);
    EXPECT_EQ(0, chip_.regs[0x2D] & 0xC0); // overrun flag cleared
    ASSERT_EQ(SATLINK_OK, satlink_mcp2515_receive(&dev_, &rx));
    EXPECT_EQ(0x180, rx.id);
    EXPECT_EQ(SATLINK_ERR_EMPTY, satlink_mcp2515_receive(&dev_, &rx));
}

TEST_F(Mcp2515Test, ExtendedFramesAreDropped)
{
    ASSERT_EQ(SATLINK_OK, Init(SATLINK_MCP2515_NORMAL));
    chip_.Inject(0x100, {1});
    chip_.regs[0x62] |= 0x08; // IDE
    satlink_can_frame_t rx{};
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_mcp2515_receive(&dev_, &rx));
    EXPECT_EQ(SATLINK_ERR_EMPTY, satlink_mcp2515_receive(&dev_, &rx));
}

} // namespace
} // namespace satlink::test
