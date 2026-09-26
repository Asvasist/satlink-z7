/**
 * @file test_ipc_ring.cpp
 * @brief Shared-memory SPSC ring: framing, wrap-around padding, full/empty, corruption checks,
 *        and a two-thread stress test (producer and consumer on different host cores, like
 *        Core 0 and Core 1).
 *
 * @verifies SRS-AMP-002
 */
#include <atomic>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "satlink/amp/ipc_ring.h"
#include "satlink/amp/shm.h"

namespace {

class RingTest : public ::testing::Test
{
  protected:
    static constexpr std::uint32_t kBytes = 128 + 256; // 256-byte data area
    void SetUp() override
    {
        mem_.assign(kBytes + 64, 0xEE);
        ASSERT_EQ(SATLINK_RING_OK, satlink_ring_init(&tx_, mem_.data(), kBytes));
        ASSERT_EQ(SATLINK_RING_OK, satlink_ring_attach(&rx_, mem_.data(), kBytes));
    }

    int Read(std::vector<std::uint8_t> &out, std::uint16_t &type, std::uint32_t *seq = nullptr)
    {
        out.assign(SATLINK_RING_MAX_PAYLOAD, 0);
        auto len = static_cast<std::uint16_t>(out.size());
        const int rc = satlink_ring_read(&rx_, &type, seq, out.data(), &len);
        out.resize(rc == SATLINK_RING_OK ? len : 0);
        return rc;
    }

    std::vector<std::uint8_t> mem_;
    satlink_ring_t tx_{};
    satlink_ring_t rx_{};
};

TEST_F(RingTest, DataAreaIsPowerOfTwo)
{
    EXPECT_EQ(256U, tx_.size);
    EXPECT_EQ(256U, satlink_ring_free(&tx_));
}

TEST_F(RingTest, WriteReadPreservesTypeSequenceAndPayload)
{
    const std::uint8_t a[5] = {1, 2, 3, 4, 5};
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 0x20, a, 5));
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 0x21, nullptr, 0));
    EXPECT_EQ(16U + 8U, satlink_ring_used(&tx_)); // 8 + 8 (padded), then 8

    std::vector<std::uint8_t> out;
    std::uint16_t type = 0;
    std::uint32_t seq = 99;
    ASSERT_EQ(SATLINK_RING_OK, Read(out, type, &seq));
    EXPECT_EQ(0x20, type);
    EXPECT_EQ(0U, seq);
    EXPECT_EQ(std::vector<std::uint8_t>(a, a + 5), out);
    ASSERT_EQ(SATLINK_RING_OK, Read(out, type, &seq));
    EXPECT_EQ(0x21, type);
    EXPECT_EQ(1U, seq);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(SATLINK_RING_EMPTY, Read(out, type));
}

TEST_F(RingTest, FullRingRefusesAndRecovers)
{
    std::vector<std::uint8_t> payload(56, 0xAB); // 64-byte records
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 1, payload.data(), 56));
    }
    EXPECT_EQ(SATLINK_RING_FULL, satlink_ring_write(&tx_, 1, payload.data(), 1));
    std::vector<std::uint8_t> out;
    std::uint16_t type = 0;
    ASSERT_EQ(SATLINK_RING_OK, Read(out, type));
    EXPECT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 1, payload.data(), 56));
}

TEST_F(RingTest, RecordsNeverStraddleTheEnd)
{
    std::vector<std::uint8_t> out;
    std::uint16_t type = 0;
    // Advance to offset 200, then a 100-byte record must wrap with padding.
    std::vector<std::uint8_t> filler(192, 1);
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 1, filler.data(), 192));
    ASSERT_EQ(SATLINK_RING_OK, Read(out, type));
    std::vector<std::uint8_t> big(92);
    for (std::size_t i = 0; i < big.size(); ++i)
    {
        big[i] = static_cast<std::uint8_t>(i);
    }
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 7, big.data(), 92));
    ASSERT_EQ(SATLINK_RING_OK, Read(out, type));
    EXPECT_EQ(7, type);
    EXPECT_EQ(big, out);
    EXPECT_EQ(0U, satlink_ring_used(&rx_));
}

TEST_F(RingTest, SmallBufferLeavesMessageQueued)
{
    const std::uint8_t a[10] = {};
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_write(&tx_, 3, a, 10));
    std::uint8_t buf[4];
    std::uint16_t len = sizeof(buf);
    std::uint16_t type = 0;
    EXPECT_EQ(SATLINK_RING_TOO_BIG, satlink_ring_read(&rx_, &type, nullptr, buf, &len));
    EXPECT_EQ(10, len);
    std::vector<std::uint8_t> out;
    EXPECT_EQ(SATLINK_RING_OK, Read(out, type));
}

TEST_F(RingTest, RejectsBadArgumentsAndCorruption)
{
    std::vector<std::uint8_t> big(SATLINK_RING_MAX_PAYLOAD + 1);
    EXPECT_EQ(SATLINK_RING_TOO_BIG,
              satlink_ring_write(&tx_, 1, big.data(), static_cast<std::uint16_t>(big.size())));
    EXPECT_EQ(SATLINK_RING_CORRUPT, satlink_ring_write(&tx_, SATLINK_RING_PAD_TYPE, nullptr, 0));

    satlink_ring_t other{};
    std::vector<std::uint8_t> blank(kBytes, 0);
    EXPECT_EQ(SATLINK_RING_CORRUPT, satlink_ring_attach(&other, blank.data(), kBytes));
    EXPECT_EQ(SATLINK_RING_CORRUPT, satlink_ring_attach(&other, mem_.data(), kBytes * 2));
    EXPECT_EQ(SATLINK_RING_CORRUPT, satlink_ring_init(&other, blank.data(), 100));

    // A head index beyond the data size means the other side is corrupt.
    tx_.hdr->head = 1000;
    std::vector<std::uint8_t> out;
    std::uint16_t type = 0;
    EXPECT_EQ(SATLINK_RING_CORRUPT, Read(out, type));
}

TEST(RingStressTest, TwoThreadsExchangeOneHundredThousandMessages)
{
    constexpr std::uint32_t kBytes = SATLINK_RING_HDR_BYTES + 4096;
    std::vector<std::uint8_t> mem(kBytes);
    satlink_ring_t prod{};
    satlink_ring_t cons{};
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_init(&prod, mem.data(), kBytes));
    ASSERT_EQ(SATLINK_RING_OK, satlink_ring_attach(&cons, mem.data(), kBytes));
    constexpr std::uint32_t kCount = 100000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        std::uint8_t buf[300];
        for (std::uint32_t i = 0; i < kCount; ++i)
        {
            const auto len = static_cast<std::uint16_t>(i % 300);
            for (std::uint16_t k = 0; k < len; ++k)
            {
                buf[k] = static_cast<std::uint8_t>(i + k);
            }
            while (satlink_ring_write(&prod, static_cast<std::uint16_t>(i & 0x7FFF), buf, len) ==
                   SATLINK_RING_FULL)
            {
                std::this_thread::yield();
            }
        }
    });

    std::uint8_t buf[SATLINK_RING_MAX_PAYLOAD];
    for (std::uint32_t i = 0; i < kCount && !failed; ++i)
    {
        std::uint16_t type = 0;
        std::uint32_t seq = 0;
        std::uint16_t len = sizeof(buf);
        int rc = SATLINK_RING_EMPTY;
        while ((rc = satlink_ring_read(&cons, &type, &seq, buf, &len)) == SATLINK_RING_EMPTY)
        {
            std::this_thread::yield();
        }
        bool ok = rc == SATLINK_RING_OK && seq == i && type == (i & 0x7FFF) && len == i % 300;
        for (std::uint16_t k = 0; ok && k < len; ++k)
        {
            ok = buf[k] == static_cast<std::uint8_t>(i + k);
        }
        if (!ok)
        {
            failed = true;
            ADD_FAILURE() << "message " << i << " rc " << rc;
        }
    }
    producer.join();
    EXPECT_FALSE(failed);
}

TEST(ShmLayoutTest, RegionsFitAndDoNotOverlap)
{
    static_assert(sizeof(satlink_ring_hdr_t) == SATLINK_RING_HDR_BYTES);
    static_assert(sizeof(satlink_shm_ctrl_t) == 256);
    EXPECT_LE(sizeof(satlink_shm_ctrl_t), SATLINK_SHM_TO_RTOS_OFFSET);
    EXPECT_EQ(SATLINK_SHM_TO_RTOS_OFFSET + SATLINK_SHM_RING_BYTES, SATLINK_SHM_TO_LINUX_OFFSET);
    EXPECT_LE(SATLINK_SHM_TO_LINUX_OFFSET + SATLINK_SHM_RING_BYTES, SATLINK_SHM_SIZE);
}

} // namespace
