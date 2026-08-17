#pragma once

#include "polygon.h"

#include <cstdint>
#include <span>

namespace poly_paint::detail
{
    using MutableRgbaSpan = std::span<std::uint8_t>;

    // Implemented by either the portable scalar backend or the AVX2 backend,
    // selected at configure time by POLY_PAINT_ENABLE_AVX2.
    void fill_rgba_pixels(std::span<std::uint8_t> rgba, RgbaColor color);
    // Every destination pixel must be opaque; blending preserves its alpha byte.
    void blend_source_over_opaque(
        std::span<const MutableRgbaSpan> rgba_spans,
        RgbaColor source);
    [[nodiscard]] std::uint64_t rgb_absolute_difference(
        std::span<const std::uint8_t> left,
        std::span<const std::uint8_t> right);
}
