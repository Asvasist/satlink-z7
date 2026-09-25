/**
 * @file test_axi_dma.cpp
 * @brief AXI DMA scatter-gather ring against a behavioural model of the engine: descriptors
 *        processed from CURDESC up to TAILDESC, completion bits, interrupts, idle on catch-up.
 *
 * @verifies SRS-AMP-006
 */
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "rtos/axi_dma.h"

namespace {

constexpr std::uint32_t kBdPhys = 0x39100000U;
constexpr std::uint32_t kBufPhys = 0x39200000U;
constexpr std::uint32_t kCount = 4;
constexpr std::uint32_t kBufBytes = 64;

class DmaEngine
{
  public:
    DmaEngine(axidma_bd_t *bds, std::uint8_t *bufs) : bds_(bds), bufs_(bufs) {}

    static std::uint32_t Read(void *ctx, std::uint32_t off)
    {
        auto *self = static_cast<DmaEngine *>(ctx);
        if (off == AXIDMA_MM2S_DMACR || off == AXIDMA_S2MM_DMACR)
        {
            return self->resetting_ ? AXIDMA_CR_RESET : self->cr_;
        }
        if (off == AXIDMA_MM2S_DMASR || off == AXIDMA_S2MM_DMASR)
        {
            return self->sr_ | (self->idle_ ? AXIDMA_SR_IDLE : 0U) |
                   (self->running_ ? 0U : AXIDMA_SR_HALTED);
        }
        return 0;
    }

    static void Write(void *ctx, std::uint32_t off, std::uint32_t v)
    {
        auto *self = static_cast<DmaEngine *>(ctx);
        switch (off)
        {
        case AXIDMA_MM2S_DMACR:
        case AXIDMA_S2MM_DMACR:
            self->resetting_ = (v & AXIDMA_CR_RESET) != 0U && !self->reset_completes_;
            self->cr_ = v & ~static_cast<std::uint32_t>(AXIDMA_CR_RESET);
            self->running_ = (v & AXIDMA_CR_RS) != 0U;
            break;
        case AXIDMA_MM2S_DMASR:
        case AXIDMA_S2MM_DMASR:
            self->sr_ &= ~v; // W1C
            break;
        case AXIDMA_MM2S_CURDESC:
        case AXIDMA_S2MM_CURDESC:
            self->pos_ = static_cast<std::uint32_t>((v - kBdPhys) / sizeof(axidma_bd_t));
            break;
        case AXIDMA_MM2S_TAILDESC:
        case AXIDMA_S2MM_TAILDESC:
            self->tail_ = static_cast<std::uint32_t>((v - kBdPhys) / sizeof(axidma_bd_t));
            self->idle_ = false;
            break;
        default:
            break;
        }
    }

    /// Process up to @p n descriptors. S2MM fills them with @p fill + index; MM2S records data.
    void Run(int n, bool s2mm)
    {
        for (int i = 0; i < n && running_ && !idle_; ++i)
        {
            axidma_bd_t &bd = bds_[pos_];
            if (bd.status & AXIDMA_BD_STS_CMPLT)
            {
                sr_ |= 0x40U | AXIDMA_SR_ERR_IRQ; // SGIntErr: descriptor not re-armed
                running_ = false;
                return;
            }
            std::uint8_t *buf = bufs_ + (bd.buffer - kBufPhys);
            const std::uint32_t len = bd.control & AXIDMA_BD_CTRL_LEN_MASK;
            if (s2mm)
            {
                std::memset(buf, static_cast<int>(counter_), len);
            }
            else
            {
                sent.emplace_back(buf, buf + len);
            }
            ++counter_;
            bd.status = AXIDMA_BD_STS_CMPLT | len | fail_status_;
            sr_ |= AXIDMA_SR_IOC_IRQ;
            if (pos_ == tail_)
            {
                idle_ = true;
            }
            pos_ = (pos_ + 1) % kCount;
        }
    }

    std::vector<std::vector<std::uint8_t>> sent;
    bool reset_completes_ = true;
    std::uint32_t fail_status_ = 0;

  private:
    axidma_bd_t *bds_;
    std::uint8_t *bufs_;
    std::uint32_t cr_ = 0;
    std::uint32_t sr_ = 0;
    std::uint32_t pos_ = 0;
    std::uint32_t tail_ = 0;
    std::uint32_t counter_ = 0;
    bool running_ = false;
    bool idle_ = true;
    bool resetting_ = false;
};

class AxiDmaTest : public ::testing::Test
{
  protected:
    AxiDmaTest()
        : bds_(kCount), bufs_(static_cast<std::size_t>(kCount) * kBufBytes),
          engine_(bds_.data(), bufs_.data())
    {
        regs_ = {&engine_, &DmaEngine::Read, &DmaEngine::Write};
    }

    void Init(bool s2mm)
    {
        axidma_ring_init(&ring_, &regs_, s2mm, bds_.data(), kBdPhys, kCount, bufs_.data(), kBufPhys,
                         kBufBytes);
    }

    std::vector<axidma_bd_t> bds_;
    std::vector<std::uint8_t> bufs_;
    DmaEngine engine_;
    axidma_regs_t regs_{};
    axidma_ring_t ring_{};
};

TEST_F(AxiDmaTest, DescriptorsFormACircle)
{
    Init(true);
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        EXPECT_EQ(kBdPhys + ((i + 1) % kCount) * 64U, bds_[i].nxtdesc);
        EXPECT_EQ(kBufPhys + i * kBufBytes, bds_[i].buffer);
        EXPECT_EQ(kBufBytes, bds_[i].control);
    }
    EXPECT_EQ(64U, sizeof(axidma_bd_t));
}

TEST_F(AxiDmaTest, ReceiveInOrderAndRequeue)
{
    Init(true);
    axidma_ring_start(&ring_);
    engine_.Run(3, true);
    EXPECT_TRUE(axidma_ring_ack(&ring_) & AXIDMA_SR_IOC_IRQ);
    for (int i = 0; i < 3; ++i)
    {
        std::uint32_t len = 0;
        std::uint8_t *buf = axidma_ring_peek_done(&ring_, &len);
        ASSERT_NE(nullptr, buf);
        EXPECT_EQ(kBufBytes, len);
        EXPECT_EQ(i, buf[0]);
        axidma_ring_requeue(&ring_);
    }
    EXPECT_EQ(nullptr, axidma_ring_peek_done(&ring_, nullptr));
    // The engine keeps going round the ring.
    engine_.Run(4, true);
    std::uint32_t len = 0;
    ASSERT_NE(nullptr, axidma_ring_peek_done(&ring_, &len));
    EXPECT_EQ(0U, ring_.errors);
}

TEST_F(AxiDmaTest, CatchingUpWithTheTailIsDetected)
{
    Init(true);
    axidma_ring_start(&ring_);
    engine_.Run(10, true); // only 4 armed: the engine stops at the tail
    EXPECT_TRUE(axidma_ring_check_idle(&ring_));
    EXPECT_EQ(1U, ring_.restarts);
    axidma_ring_requeue(&ring_); // re-arms descriptor 0 and moves the tail
    EXPECT_FALSE(axidma_ring_check_idle(&ring_));
    engine_.Run(1, true);
    axidma_ring_ack(&ring_);
    EXPECT_EQ(0U, ring_.errors);
}

TEST_F(AxiDmaTest, TransmitPrefilledBuffersThenRefill)
{
    Init(false);
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        std::memset(axidma_ring_buffer(&ring_, i), static_cast<int>(0xA0 + i), kBufBytes);
    }
    EXPECT_EQ(AXIDMA_BD_CTRL_SOF | AXIDMA_BD_CTRL_EOF | kBufBytes, bds_[0].control);
    axidma_ring_start(&ring_);
    engine_.Run(2, false);
    axidma_ring_ack(&ring_);
    for (int i = 0; i < 2; ++i)
    {
        std::uint8_t *buf = axidma_ring_peek_done(&ring_, nullptr);
        ASSERT_NE(nullptr, buf);
        std::memset(buf, 0xB0 + i, kBufBytes);
        axidma_ring_requeue(&ring_);
    }
    engine_.Run(4, false);
    ASSERT_EQ(6U, engine_.sent.size());
    const std::uint8_t expected[] = {0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xB1};
    for (std::size_t i = 0; i < 6; ++i)
    {
        EXPECT_EQ(expected[i], engine_.sent[i][0]) << i;
    }
}

TEST_F(AxiDmaTest, ErrorsAreCounted)
{
    Init(true);
    axidma_ring_start(&ring_);
    engine_.fail_status_ = 0x20000000U; // DMASlvErr in the descriptor
    engine_.Run(1, true);
    ASSERT_NE(nullptr, axidma_ring_peek_done(&ring_, nullptr));
    axidma_ring_requeue(&ring_);
    EXPECT_EQ(1U, ring_.errors);

    // Moving the tail over a descriptor the software never re-armed stops the engine with an
    // SG error, which the interrupt acknowledgement counts.
    engine_.fail_status_ = 0;
    engine_.Run(10, true); // completes 1, 2, 3, 0 and idles at the tail
    DmaEngine::Write(&engine_, AXIDMA_S2MM_TAILDESC, kBdPhys + 2 * 64U);
    engine_.Run(1, true);
    EXPECT_TRUE(axidma_ring_ack(&ring_) & AXIDMA_SR_ERR_IRQ);
    EXPECT_EQ(2U, ring_.errors);
}

TEST_F(AxiDmaTest, ResetTimesOut)
{
    EXPECT_TRUE(axidma_reset(&regs_));
    engine_.reset_completes_ = false;
    EXPECT_FALSE(axidma_reset(&regs_));
}

} // namespace
