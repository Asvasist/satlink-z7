/**
 * @file test_hkc_image.cpp
 * @brief HKC image header format and every rejection path of satlink_hkc_image_verify().
 *
 * @verifies SRS-HKC-004
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/hkc/image.h"

#include "image_builder.hpp"

namespace satlink::test {
namespace {

satlink_hkc_image_result_t Verify(const std::vector<std::uint8_t> &image)
{
    return satlink_hkc_image_verify(image.data(), image.size(), kAppBase, kAppSize, nullptr);
}

TEST(HkcImageTest, ValidImagePassesAndHeaderDecodes)
{
    const auto image = BuildImage(1000);
    satlink_hkc_image_header_t header{};
    ASSERT_EQ(SATLINK_HKC_IMAGE_OK,
              satlink_hkc_image_verify(image.data(), image.size(), kAppBase, kAppSize, &header));
    EXPECT_EQ(1000U, header.payload_size);
    EXPECT_EQ(kAppBase + 32U, header.entry);
    EXPECT_EQ(2U, header.version_minor);
}

TEST(HkcImageTest, MagicIsSlhkLittleEndian)
{
    const auto image = BuildImage(4);
    EXPECT_EQ('S', image[0]);
    EXPECT_EQ('L', image[1]);
    EXPECT_EQ('H', image[2]);
    EXPECT_EQ('K', image[3]);
}

TEST(HkcImageTest, RejectsShortInput)
{
    const auto image = BuildImage(100);
    const std::vector<std::uint8_t> header_only(image.begin(), image.begin() + 20);
    EXPECT_EQ(SATLINK_HKC_IMAGE_TOO_SHORT, Verify(header_only));
    const std::vector<std::uint8_t> truncated(image.begin(), image.end() - 1);
    EXPECT_EQ(SATLINK_HKC_IMAGE_TOO_SHORT, Verify(truncated));
    EXPECT_EQ(SATLINK_HKC_IMAGE_TOO_SHORT,
              satlink_hkc_image_verify(nullptr, 0, kAppBase, kAppSize, nullptr));
}

TEST(HkcImageTest, RejectsBadMagicVersionAndHeaderCrc)
{
    auto image = BuildImage(16);
    EditHeader(image, [](auto &h) { h.magic = 0xDEADBEEFU; });
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_MAGIC, Verify(image));

    image = BuildImage(16);
    EditHeader(image, [](auto &h) { h.header_version = 2; });
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_HEADER, Verify(image));

    image = BuildImage(16);
    image[25] ^= 0x01U; // version_minor, without fixing the header CRC
    EXPECT_EQ(SATLINK_HKC_IMAGE_HEADER_CRC, Verify(image));
}

TEST(HkcImageTest, RejectsWrongLocation)
{
    auto image = BuildImage(16, 0, kAppBase + 4);
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_LOCATION, Verify(image));

    image = BuildImage(16);
    EditHeader(image, [](auto &h) { h.entry = kAppBase; }); // points at the header
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_LOCATION, Verify(image));

    image = BuildImage(16);
    EditHeader(image, [](auto &h) { h.entry = kAppBase + 32 + 16; }); // one past the payload
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_LOCATION, Verify(image));

    image = BuildImage(16);
    EditHeader(image, [](auto &h) { h.payload_size = 0xFFFFFFF0U; }); // must not wrap
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_LOCATION, Verify(image));
}

TEST(HkcImageTest, RejectsImageLargerThanRegion)
{
    const auto image = BuildImage(kAppSize); // header + payload exceeds the region by 32
    EXPECT_EQ(SATLINK_HKC_IMAGE_BAD_LOCATION, Verify(image));
    const auto fits = BuildImage(kAppSize - 32);
    EXPECT_EQ(SATLINK_HKC_IMAGE_OK, Verify(fits));
}

TEST(HkcImageTest, RejectsCorruptPayload)
{
    auto image = BuildImage(500);
    image[300] ^= 0x80U;
    EXPECT_EQ(SATLINK_HKC_IMAGE_PAYLOAD_CRC, Verify(image));
}

} // namespace
} // namespace satlink::test
