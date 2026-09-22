/**
 * @file regdef.hpp
 * @brief constexpr register/field descriptors used by the generated C++ headers.
 *
 * Lets C++ code (HAL, tests) manipulate register fields with compile-time checked
 * descriptors instead of magic numbers.
 *
 * @implements SRS-ICD-001
 */
#pragma once

#include <cstdint>

namespace satlink::regs {

/** Software access semantics of a register or field. */
enum class Access : std::uint8_t
{
    kReadOnly,
    kReadWrite,
    kWriteOnly,
    kWrite1Clear,
};

/** A 32-bit register at a byte offset from the block base address. */
struct Register
{
    std::uint32_t offset;
    Access access;
    std::uint32_t reset;
};

/** A contiguous bit field inside a register. */
struct Field
{
    std::uint32_t offset; /**< Offset of the containing register. */
    std::uint32_t shift;  /**< Position of the least significant bit. */
    std::uint32_t width;  /**< Number of bits (1..32). */
    Access access;

    /** Bit mask of the field in register position. */
    [[nodiscard]] constexpr std::uint32_t mask() const noexcept
    {
        return (width >= 32U) ? 0xFFFFFFFFU : (((1U << width) - 1U) << shift);
    }

    /** Field value contained in @p reg. */
    [[nodiscard]] constexpr std::uint32_t get(std::uint32_t reg) const noexcept
    {
        return (reg & mask()) >> shift;
    }

    /** @p reg with the field replaced by @p value (excess bits dropped). */
    [[nodiscard]] constexpr std::uint32_t set(std::uint32_t reg, std::uint32_t value) const noexcept
    {
        return (reg & ~mask()) | ((value << shift) & mask());
    }

    /** True if @p value is representable in the field. */
    [[nodiscard]] constexpr bool fits(std::uint32_t value) const noexcept
    {
        return (width >= 32U) || (value < (1U << width));
    }
};

} // namespace satlink::regs
