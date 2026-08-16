#include "image_similarity_scorer.h"
#include "image_dimensions.h"
#include "polygon.h"
#include "polygon_evolution.h"
#include "polygon_rasterizer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
    [[nodiscard]] bool run_tests()
    {
        constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();
        const std::array vertices {
            poly_paint::PolygonPoint {0.0f, 0.0f},
            poly_paint::PolygonPoint {1.0f, 0.0f},
            poly_paint::PolygonPoint {1.0f, 1.0f},
            poly_paint::PolygonPoint {0.0f, 1.0f}
        };
        poly_paint::PolygonCollection polygons(1);
        polygons.add(poly_paint::Polygon(vertices, {200, 10, 20, maximum_channel}));

        const std::vector<std::uint8_t> image =
            poly_paint::rasterize(polygons, 2, 2);
        const std::array expected_pixel {std::uint8_t {200}, std::uint8_t {10},
            std::uint8_t {20}, maximum_channel};
        for (std::size_t offset = 0; offset < image.size(); offset += expected_pixel.size())
        {
            if (!std::ranges::equal(
                    std::span<const std::uint8_t> {image}.subspan(offset, expected_pixel.size()),
                    expected_pixel))
            {
                std::cerr << "full-image raster output was incorrect\n";
                return false;
            }
        }

        std::array<std::uint8_t, 2 * poly_paint::rgba_channel_count> row {};
        poly_paint::rasterize_rows_into(polygons, 2, 2, 1, 1, row);
        if (!std::ranges::equal(row, std::span<const std::uint8_t> {image}.last(row.size())))
        {
            std::cerr << "tiled raster output differed from full-image output\n";
            return false;
        }

        const poly_paint::ImageSimilarityScorer scorer(2, 2, image);
        if (scorer.score(image, 2, 2) != 1.0)
        {
            std::cerr << "identical images did not receive a perfect score\n";
            return false;
        }

        std::vector<std::uint8_t> changed = image;
        changed[poly_paint::alpha_channel_index] = 0;
        if (scorer.score(changed, 2, 2) != 1.0)
        {
            std::cerr << "alpha unexpectedly affected RGB similarity\n";
            return false;
        }
        changed[0] = 0;
        if (scorer.score(changed, 2, 2) >= 1.0)
        {
            std::cerr << "an RGB change did not reduce similarity\n";
            return false;
        }

        const std::array<std::uint8_t, poly_paint::rgba_channel_count> tiny_image {
            0, 0, 0, maximum_channel};
        const std::vector<poly_paint::ContrastSeed> no_seeds =
            poly_paint::find_high_contrast_seeds(tiny_image, 1, 1, 3);
        std::mt19937 random_engine(42);
        const poly_paint::PolygonCollection fallback =
            poly_paint::make_best_guess_polygon_collection(no_seeds, 1, 1, 3, random_engine);
        if (!fallback.full())
        {
            std::cerr << "best-guess initialization did not fill a sparse seed population\n";
            return false;
        }

        bool overflow_rejected = false;
        try
        {
            [[maybe_unused]] const std::size_t impossible_size =
                poly_paint::checked_rgba_byte_count(
                    std::numeric_limits<std::size_t>::max(), 2);
        }
        catch (const std::overflow_error&)
        {
            overflow_rejected = true;
        }
        if (!overflow_rejected)
        {
            std::cerr << "overflowing image dimensions were accepted\n";
            return false;
        }
        return true;
    }
}

int main()
{
    try
    {
        return run_tests() ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    }
}
