/**
 * @file image_builder.hpp
 * @brief Builds HKC firmware images for the tests (the same job tools/hkc/mkimage.py does).
 */
#pragma once

#include <cstdint>
#include <vector>

#include "satlink/common/crc32.h"
#include "satlink/hkc/image.h"

namespace satlink::test {

inline constexpr std::uint32_t kAppBase = 0x00004000U;
inline constexpr std::uint32_t kAppSize = 0x0001C000U;

inline std::vector<std::uint8_t> BuildImage(std::uint32_t payload_size, std::uint8_t fill = 0x5A,
                                            std::uint32_t load_addr = kAppBase)
{
    std::vector<std::uint8_t> payload(payload_size);
    for (std::uint32_t i = 0; i < payload_size; ++i)
    {
        payload[i] = static_cast<std::uint8_t>(fill + i);
    }
    satlink_hkc_image_header_t header{};
    header.magic = SATLINK_HKC_IMAGE_MAGIC;
    header.header_version = SATLINK_HKC_IMAGE_HEADER_VERSION;
    header.header_size = SATLINK_HKC_IMAGE_HEADER_SIZE;
    header.load_addr = load_addr;
    header.entry = load_addr + SATLINK_HKC_IMAGE_HEADER_SIZE;
    header.payload_size = payload_size;
    header.payload_crc32 = satlink_crc32(payload.data(), payload.size());
    header.version_major = 1;
    header.version_minor = 2;
    header.version_patch = 3;

    std::vector<std::uint8_t> image(SATLINK_HKC_IMAGE_HEADER_SIZE);
    satlink_hkc_image_write_header(&header, image.data());
    image.insert(image.end(), payload.begin(), payload.end());
    return image;
}

/// Rewrites the header (and its CRC) of @p image after @p edit changed the decoded fields.
template <typename Edit> void EditHeader(std::vector<std::uint8_t> &image, Edit edit)
{
    satlink_hkc_image_header_t header{};
    satlink_hkc_image_read_header(image.data(), image.size(), &header);
    edit(header);
    satlink_hkc_image_write_header(&header, image.data());
}

} // namespace satlink::test
