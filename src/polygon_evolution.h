#pragma once

#include "polygon.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace poly_paint
{
    /** @brief A high-contrast target pixel used to seed a polygon. */
    struct ContrastSeed
    {
        /** @brief Horizontal source-pixel coordinate. */
        std::size_t x {};
        /** @brief Vertical source-pixel coordinate. */
        std::size_t y {};
        /** @brief Source-pixel color at this location. */
        RgbaColor color {};
        /** @brief Measured local color contrast. */
        std::uint16_t contrast {};
    };

    /** @brief Creates a randomized polygon collection with @p polygon_count polygons. */
    [[nodiscard]] PolygonCollection make_random_polygon_collection(
        std::mt19937& random_engine,
        std::size_t polygon_count);

    /** @brief Finds up to @p polygon_count high-contrast seeds in an RGBA image. */
    [[nodiscard]] std::vector<ContrastSeed> find_high_contrast_seeds(
        std::span<const std::uint8_t> rgba,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count);

    /** @brief Creates a polygon collection initialized from contrast @p seeds. */
    [[nodiscard]] PolygonCollection make_best_guess_polygon_collection(
        std::span<const ContrastSeed> seeds,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count,
        std::mt19937& random_engine);

    /** @brief Returns a randomized mutation of @p parent. */
    [[nodiscard]] PolygonCollection mutate_polygon_collection(
        const PolygonCollection& parent,
        std::mt19937& random_engine);
}
