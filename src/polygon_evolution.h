#pragma once

#include "polygon.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace poly_paint
{
    struct ContrastSeed
    {
        std::size_t x {};
        std::size_t y {};
        RgbaColor color {};
        std::uint16_t contrast {};
    };

    [[nodiscard]] PolygonCollection make_random_polygon_collection(
        std::mt19937& random_engine,
        std::size_t polygon_count);

    [[nodiscard]] std::vector<ContrastSeed> find_high_contrast_seeds(
        std::span<const std::uint8_t> rgba,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count);

    [[nodiscard]] PolygonCollection make_best_guess_polygon_collection(
        std::span<const ContrastSeed> seeds,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count,
        std::mt19937& random_engine);

    [[nodiscard]] PolygonCollection mutate_polygon_collection(
        const PolygonCollection& parent,
        std::mt19937& random_engine);
}
