#pragma once

#include "polygon.h"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace poly_paint
{
    // Converts a polygon genome into a row-major RGBA image. Polygons are drawn
    // in collection order: later polygons are composited over earlier ones.
    [[nodiscard]] std::vector<std::uint8_t> rasterize(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        RgbaColor background = {
            0, 0, 0, std::numeric_limits<std::uint8_t>::max()});

    void rasterize_into(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        std::span<std::uint8_t> output_rgba,
        RgbaColor background = {
            0, 0, 0, std::numeric_limits<std::uint8_t>::max()});

    // Rasterizes a contiguous set of image rows into a compact output buffer.
    void rasterize_rows_into(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t image_height,
        std::size_t first_row,
        std::size_t row_count,
        std::span<std::uint8_t> output_rgba,
        RgbaColor background = {
            0, 0, 0, std::numeric_limits<std::uint8_t>::max()});
}
