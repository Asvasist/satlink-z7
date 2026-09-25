/**
 * @file test_canboot.cpp
 * @brief CAN bootloader session: complete downloads, lost ACKs, out-of-order and corrupt data,
 *        and protocol misuse.
 *
 * @verifies SRS-HKC-002
 */
#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/common/byte_order.h"
#include "satlink/common/crc32.h"
#include "satlink/hkc/canboot.h"

#include "image_builder.hpp"

namespace satlink::test {
namespace {

class CanbootTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        region_.assign(kAppSize, 0xFF);
        ASSERT_EQ(SATLINK_OK,
                  satlink_canboot_target_init(&target_, region_.data(), kAppBase, kAppSize, 1, 4));
    }

    satlink_can_frame_t Send(std::uint8_t op, std::vector<std::uint8_t> payload = {})
    {
        satlink_can_frame_t request{};
        EXPECT_EQ(SATLINK_OK,
                  satlink_canboot_encode_request(
                      op, payload.data(), static_cast<std::uint8_t>(payload.size()), &request));
        satlink_can_frame_t response{};
        EXPECT_TRUE(satlink_canboot_target_handle(&target_, &request, &response));
        EXPECT_EQ(SATLINK_CANBOOT_ID_RESPONSE, response.id);
        EXPECT_EQ(op | SATLINK_CANBOOT_RESPONSE_FLAG, response.data[0]);
        return response;
    }

    static std::vector<std::uint8_t> U32(std::uint32_t value)
    {
        std::vector<std::uint8_t> bytes(4);
        satlink_put_le32(bytes.data(), value);
        return bytes;
    }

    std::uint8_t Start(std::uint32_t size)
    {
        return Send(SATLINK_CANBOOT_OP_START, U32(size)).data[1];
    }

    std::uint8_t Data(std::uint8_t seq, const std::uint8_t *bytes, std::size_t len)
    {
        std::vector<std::uint8_t> payload{seq};
        payload.insert(payload.end(), bytes, bytes + len);
        return Send(SATLINK_CANBOOT_OP_DATA, payload).data[1];
    }

    std::uint8_t End(std::uint32_t crc)
    {
        return Send(SATLINK_CANBOOT_OP_END, U32(crc)).data[1];
    }

    /// Streams @p image in 6-byte chunks; returns the first non-OK status (or OK).
    std::uint8_t Download(const std::vector<std::uint8_t> &image)
    {
        std::uint8_t status = Start(static_cast<std::uint32_t>(image.size()));
        std::uint8_t seq = 0;
        for (std::size_t off = 0; (status == SATLINK_CANBOOT_ST_OK) && (off < image.size());
             off += SATLINK_CANBOOT_CHUNK)
        {
            const std::size_t len =
                std::min<std::size_t>(SATLINK_CANBOOT_CHUNK, image.size() - off);
            status = Data(seq++, &image[off], len);
        }
        if (status == SATLINK_CANBOOT_ST_OK)
        {
            status = End(satlink_crc32(image.data(), image.size()));
        }
        return status;
    }

    std::vector<std::uint8_t> region_;
    satlink_canboot_target_t target_{};
};

TEST_F(CanbootTest, PingReportsVersionAndNoValidImageOnBlankRegion)
{
    const auto r = Send(SATLINK_CANBOOT_OP_PING);
    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, r.data[1]);
    EXPECT_EQ(1, r.data[2]);
    EXPECT_EQ(4, r.data[3]);
    EXPECT_EQ(0, r.data[4]); // no valid image
    EXPECT_EQ(SATLINK_CANBOOT_IDLE, r.data[5]);
}

TEST_F(CanbootTest, FullDownloadThenBoot)
{
    const auto image = BuildImage(4001); // not a multiple of the chunk size
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Download(image));
    EXPECT_TRUE(std::equal(image.begin(), image.end(), region_.begin()));
    EXPECT_EQ(1, Send(SATLINK_CANBOOT_OP_PING).data[4]);

    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, Send(SATLINK_CANBOOT_OP_BOOT).data[1]);
    EXPECT_TRUE(target_.boot_requested);
    EXPECT_EQ(kAppBase + 32U, target_.entry);
}

TEST_F(CanbootTest, ExistingImageIsDetectedAtInit)
{
    const auto image = BuildImage(64);
    std::copy(image.begin(), image.end(), region_.begin());
    ASSERT_EQ(SATLINK_OK,
              satlink_canboot_target_init(&target_, region_.data(), kAppBase, kAppSize, 1, 4));
    EXPECT_TRUE(target_.image_valid);
    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, Send(SATLINK_CANBOOT_OP_BOOT).data[1]);
}

TEST_F(CanbootTest, RetransmittedFrameIsAcknowledgedButNotWrittenTwice)
{
    const auto image = BuildImage(40);
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(static_cast<std::uint32_t>(image.size())));
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Data(0, &image[0], 6));
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Data(0, &image[0], 6)); // ACK was lost, host repeats
    EXPECT_EQ(6U, target_.received);
    std::uint8_t seq = 1;
    for (std::size_t off = 6; off < image.size(); off += 6)
    {
        ASSERT_EQ(SATLINK_CANBOOT_ST_OK,
                  Data(seq++, &image[off], std::min<std::size_t>(6, image.size() - off)));
    }
    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, End(satlink_crc32(image.data(), image.size())));
}

TEST_F(CanbootTest, OutOfOrderFrameIsRejected)
{
    const auto image = BuildImage(40);
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(static_cast<std::uint32_t>(image.size())));
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Data(0, &image[0], 6));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_SEQ, Data(2, &image[6], 6));
}

TEST_F(CanbootTest, SequenceNumberWrapsAfter256Frames)
{
    const auto image = BuildImage(6 * 300);
    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, Download(image));
}

TEST_F(CanbootTest, WrongCrcFailsAndImageIsNotBootable)
{
    const auto image = BuildImage(100);
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(static_cast<std::uint32_t>(image.size())));
    std::uint8_t seq = 0;
    for (std::size_t off = 0; off < image.size(); off += 6)
    {
        ASSERT_EQ(SATLINK_CANBOOT_ST_OK,
                  Data(seq++, &image[off], std::min<std::size_t>(6, image.size() - off)));
    }
    const auto r = Send(SATLINK_CANBOOT_OP_END, U32(0x12345678U));
    EXPECT_EQ(SATLINK_CANBOOT_ST_CRC, r.data[1]);
    EXPECT_EQ(satlink_crc32(image.data(), image.size()), satlink_get_le32(&r.data[2]));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_IMAGE, Send(SATLINK_CANBOOT_OP_BOOT).data[1]);
}

TEST_F(CanbootTest, CorrectCrcButInvalidImageIsRejected)
{
    auto image = BuildImage(100);
    image[0] = 'X'; // bad magic; the transport CRC still matches what was sent
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_IMAGE, Download(image));
    EXPECT_FALSE(target_.image_valid);
}

TEST_F(CanbootTest, StartInvalidatesThePreviousImage)
{
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Download(BuildImage(64)));
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(100));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_IMAGE, Send(SATLINK_CANBOOT_OP_BOOT).data[1]);
    EXPECT_EQ(SATLINK_CANBOOT_ST_OK, Send(SATLINK_CANBOOT_OP_ABORT).data[1]);
    EXPECT_EQ(SATLINK_CANBOOT_IDLE, target_.state);
}

TEST_F(CanbootTest, RejectsOversizeAndMisuse)
{
    EXPECT_EQ(SATLINK_CANBOOT_ST_TOO_LARGE, Start(kAppSize + 1));
    EXPECT_EQ(SATLINK_CANBOOT_ST_TOO_LARGE, Start(10)); // smaller than a header
    const std::uint8_t byte = 0;
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_STATE, Data(0, &byte, 1));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_STATE, End(0));
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(40));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_STATE, Start(40));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_LENGTH, End(0)); // not all bytes received
    EXPECT_EQ(SATLINK_CANBOOT_ST_UNKNOWN, Send(0x42).data[1]);
}

TEST_F(CanbootTest, DataBeyondAnnouncedSizeIsRejected)
{
    ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Start(40));
    const std::uint8_t bytes[6] = {};
    for (std::uint8_t seq = 0; seq < 6; ++seq)
    {
        ASSERT_EQ(SATLINK_CANBOOT_ST_OK, Data(seq, bytes, 6)); // 36 bytes
    }
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_LENGTH, Data(6, bytes, 6)); // would be 42
}

TEST_F(CanbootTest, IgnoresOtherIdsAndHandlesEmptyFrames)
{
    satlink_can_frame_t request{};
    satlink_can_frame_t response{};
    request.id = 0x100;
    request.dlc = 1;
    EXPECT_FALSE(satlink_canboot_target_handle(&target_, &request, &response));
    request.id = SATLINK_CANBOOT_ID_REQUEST;
    request.dlc = 0;
    ASSERT_TRUE(satlink_canboot_target_handle(&target_, &request, &response));
    EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_LENGTH, response.data[1]);
}

} // namespace
} // namespace satlink::test
