#pragma once

#include "polygon.h"

#include <cstdint>
#include <span>

namespace poly_paint::detail
{
    /** @brief Mutable byte view over one compact RGBA span. */
    using MutableRgbaSpan = std::span<std::uint8_t>;

    /** @brief Fills an RGBA buffer with one color using Highway SIMD dispatch. */
    void fill_rgba_pixels(std::span<std::uint8_t> rgba, RgbaColor color);
    /** @brief Source-over blends @p source into opaque destination spans.
     *  @details Highway selects the best supported SIMD target at runtime.
     */
    void blend_source_over_opaque(
        std::span<const MutableRgbaSpan> rgba_spans,
        RgbaColor source);
    /** @brief Returns the sum of absolute differences between two RGB byte sequences. */
    [[nodiscard]] std::uint64_t rgb_absolute_difference(
        std::span<const std::uint8_t> left,
        std::span<const std::uint8_t> right);
}
