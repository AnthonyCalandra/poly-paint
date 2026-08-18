#pragma once

#include "polygon.h"

#include <cstdint>
#include <span>

namespace poly_paint::detail
{
    /** @brief Mutable byte view over one compact RGBA span. */
    using MutableRgbaSpan = std::span<std::uint8_t>;

    /** @brief Fills an RGBA buffer with one color using the configured SIMD backend. */
    void fill_rgba_pixels(std::span<std::uint8_t> rgba, RgbaColor color);
    /** @brief Source-over blends @p source into opaque destination spans.
     *  @details The configured scalar or AVX2 backend preserves each destination alpha byte.
     */
    void blend_source_over_opaque(
        std::span<const MutableRgbaSpan> rgba_spans,
        RgbaColor source);
    /** @brief Returns the sum of absolute differences between two RGB byte sequences. */
    [[nodiscard]] std::uint64_t rgb_absolute_difference(
        std::span<const std::uint8_t> left,
        std::span<const std::uint8_t> right);
}
