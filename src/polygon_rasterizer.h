#pragma once

#include "polygon.h"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace poly_paint
{
    /** @brief Converts a polygon collection into a row-major RGBA image.
     *  @details Later polygons are composited over earlier polygons.
     */
    [[nodiscard]] std::vector<std::uint8_t> rasterize(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        RgbaColor background = {
            0, 0, 0, std::numeric_limits<std::uint8_t>::max()});

    /** @brief Rasterizes a collection into the caller-provided full RGBA buffer. */
    void rasterize_into(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        std::span<std::uint8_t> output_rgba,
        RgbaColor background = {
            0, 0, 0, std::numeric_limits<std::uint8_t>::max()});

    /** @brief Rasterizes consecutive image rows into a compact RGBA output buffer. */
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
