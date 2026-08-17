#include "image_similarity_scorer.h"
#include "image_dimensions.h"
#include "polygon.h"
#include "polygon_evolution.h"
#include "polygon_rasterizer.h"
#include "pixel_kernels.h"

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
    [[nodiscard]] std::uint8_t reference_opaque_blend_channel(
        std::uint8_t destination,
        std::uint8_t source,
        std::uint8_t alpha)
    {
        constexpr std::uint32_t maximum_channel =
            std::numeric_limits<std::uint8_t>::max();
        std::uint32_t sum = source * alpha +
            destination * (maximum_channel - alpha) + 128;
        sum += sum >> 8;
        return static_cast<std::uint8_t>(sum >> 8);
    }

    [[nodiscard]] bool test_opaque_blend_kernel()
    {
        constexpr std::uint8_t maximum_channel =
            std::numeric_limits<std::uint8_t>::max();
        constexpr std::array<std::size_t, 10> pixel_counts {1, 2, 3, 4, 7, 8, 9, 15, 16, 33};
        constexpr std::array<std::uint8_t, 7> alpha_values {0, 1, 30, 60, 127, 254, 255};
        constexpr poly_paint::RgbaColor source_rgb {17, 139, 251, 0};

        for (const std::size_t pixel_count : pixel_counts)
        {
            for (const std::uint8_t alpha : alpha_values)
            {
                std::vector<std::uint8_t> actual(pixel_count * poly_paint::rgba_channel_count);
                for (std::size_t pixel = 0; pixel < pixel_count; ++pixel)
                {
                    const std::size_t offset = pixel * poly_paint::rgba_channel_count;
                    actual[offset] = static_cast<std::uint8_t>((pixel * 37 + 11) % 256);
                    actual[offset + 1] = static_cast<std::uint8_t>((pixel * 73 + 29) % 256);
                    actual[offset + 2] = static_cast<std::uint8_t>((pixel * 109 + 47) % 256);
                    actual[offset + poly_paint::alpha_channel_index] = maximum_channel;
                }

                std::vector<std::uint8_t> expected = actual;
                const poly_paint::RgbaColor source {
                    source_rgb.r, source_rgb.g, source_rgb.b, alpha};
                for (std::size_t offset = 0;
                     offset < expected.size();
                     offset += poly_paint::rgba_channel_count)
                {
                    expected[offset] =
                        reference_opaque_blend_channel(expected[offset], source.r, alpha);
                    expected[offset + 1] =
                        reference_opaque_blend_channel(expected[offset + 1], source.g, alpha);
                    expected[offset + 2] =
                        reference_opaque_blend_channel(expected[offset + 2], source.b, alpha);
                }

                std::array<poly_paint::detail::MutableRgbaSpan, 2> spans {};
                std::size_t span_count = 1;
                if (pixel_count > 8)
                {
                    constexpr std::size_t first_span_size =
                        8 * poly_paint::rgba_channel_count;
                    spans[0] = std::span<std::uint8_t> {actual}.first(first_span_size);
                    spans[1] = std::span<std::uint8_t> {actual}.subspan(first_span_size);
                    span_count = 2;
                }
                else
                {
                    spans[0] = actual;
                }
                poly_paint::detail::blend_source_over_opaque(
                    std::span<const poly_paint::detail::MutableRgbaSpan> {
                        spans.data(), span_count},
                    source);
                if (actual != expected)
                {
                    std::cerr << "opaque blend kernel differed from the scalar reference\n";
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool run_tests()
    {
        if (!test_opaque_blend_kernel())
        {
            return false;
        }

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

        const std::array left_sliver {
            poly_paint::PolygonPoint {0.0f, 0.0f},
            poly_paint::PolygonPoint {0.1f, 0.0f},
            poly_paint::PolygonPoint {0.1f, 1.0f},
            poly_paint::PolygonPoint {0.0f, 1.0f}
        };
        const std::array right_sliver {
            poly_paint::PolygonPoint {0.9f, 0.0f},
            poly_paint::PolygonPoint {1.0f, 0.0f},
            poly_paint::PolygonPoint {1.0f, 1.0f},
            poly_paint::PolygonPoint {0.9f, 1.0f}
        };
        poly_paint::PolygonCollection edge_slivers(2);
        edge_slivers.add(poly_paint::Polygon(left_sliver, {255, 0, 0, maximum_channel}));
        edge_slivers.add(poly_paint::Polygon(right_sliver, {255, 0, 0, maximum_channel}));
        const std::vector<std::uint8_t> clipped_slivers =
            poly_paint::rasterize(edge_slivers, 4, 1);
        const std::array background_pixel {
            std::uint8_t {0}, std::uint8_t {0}, std::uint8_t {0}, maximum_channel};
        for (std::size_t offset = 0;
             offset < clipped_slivers.size();
             offset += background_pixel.size())
        {
            if (!std::ranges::equal(
                    std::span<const std::uint8_t> {clipped_slivers}.subspan(
                        offset, background_pixel.size()),
                    background_pixel))
            {
                std::cerr << "subpixel edge span incorrectly covered a raster sample\n";
                return false;
            }
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
