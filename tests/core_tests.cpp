#include "image_similarity_scorer.h"
#include "image_dimensions.h"
#include "evolution_strategy.h"
#include "evolution_runner.h"
#include "polygon.h"
#include "polygon_evolution.h"
#include "polygon_rasterizer.h"
#include "pixel_kernels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] bool test_highway_fill_and_difference_kernels()
    {
        constexpr std::array<std::size_t, 15> pixel_counts {
            0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 31, 32, 33, 65, 129};
        constexpr poly_paint::RgbaColor fill_color {17, 139, 251, 43};
        for (const std::size_t pixel_count : pixel_counts)
        {
            std::vector<std::uint8_t> left(
                pixel_count * poly_paint::rgba_channel_count);
            poly_paint::detail::fill_rgba_pixels(left, fill_color);
            std::vector<std::uint8_t> right = left;
            std::uint64_t expected_difference = 0;
            for (std::size_t pixel = 0; pixel < pixel_count; ++pixel)
            {
                const std::size_t offset = pixel * poly_paint::rgba_channel_count;
                if (left[offset] != fill_color.r ||
                    left[offset + 1] != fill_color.g ||
                    left[offset + 2] != fill_color.b ||
                    left[offset + poly_paint::alpha_channel_index] != fill_color.a)
                {
                    std::cerr << "Highway RGBA fill produced an incorrect pixel\n";
                    return false;
                }

                right[offset] = static_cast<std::uint8_t>((pixel * 37 + 3) % 256);
                right[offset + 1] = static_cast<std::uint8_t>((pixel * 71 + 5) % 256);
                right[offset + 2] = static_cast<std::uint8_t>((pixel * 109 + 7) % 256);
                right[offset + poly_paint::alpha_channel_index] =
                    static_cast<std::uint8_t>((pixel * 149 + 11) % 256);
                for (std::size_t channel = 0;
                     channel < poly_paint::rgb_channel_count;
                     ++channel)
                {
                    const int difference = left[offset + channel] - right[offset + channel];
                    expected_difference += difference < 0 ? -difference : difference;
                }
            }

            if (poly_paint::detail::rgb_absolute_difference(left, right) !=
                expected_difference)
            {
                std::cerr << "Highway RGB difference differed from the scalar reference\n";
                return false;
            }
        }
        return true;
    }

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

    [[nodiscard]] bool test_evolution_migration()
    {
        using Strategy = poly_paint::EvolutionStrategy<int, int>;
        Strategy::Settings settings;
        settings.parent_count = 1;
        settings.offspring_count = 1;
        settings.max_generations = 2;
        settings.worker_count = 1;

        std::vector<int> generation_bests;
        Strategy strategy(
            settings,
            [](std::mt19937&)
            {
                return 1;
            },
            [](const int& parent, std::mt19937&)
            {
                return parent + 1;
            },
            [](const int& individual)
            {
                return individual;
            },
            {},
            [&generation_bests](std::size_t, const int& best, const int&)
            {
                generation_bests.push_back(best);
                return true;
            },
            [](std::size_t generation) -> std::optional<Strategy::Migrant>
            {
                if (generation == 1)
                {
                    return Strategy::Migrant {50, 50};
                }
                return std::nullopt;
            });

        std::mt19937 random_engine(42);
        const Strategy::Result result = strategy.run(random_engine);
        if (generation_bests != std::vector<int> {50, 51} || result.best_individual != 51)
        {
            std::cerr << "a migrant did not enter selection and produce local descendants\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool test_polygon_collection_storage()
    {
        const std::array vertices {
            poly_paint::PolygonPoint {0.0f, 0.0f},
            poly_paint::PolygonPoint {1.0f, 0.0f},
            poly_paint::PolygonPoint {0.0f, 1.0f}};
        const auto make_polygon = [&vertices](std::uint8_t red)
        {
            return poly_paint::Polygon(vertices, {red, 0, 0, 255});
        };

        poly_paint::PolygonCollection original(3);
        if (!original.add(make_polygon(10)) ||
            !original.add(make_polygon(20)) ||
            !original.add(make_polygon(30)) ||
            original.add(make_polygon(40)))
        {
            std::cerr << "polygon collection did not enforce its selected limit\n";
            return false;
        }

        poly_paint::PolygonCollection copy = original;
        copy.remove(1);
        if (copy.size() != 2 ||
            copy.polygon_at(0).color().r != 10 ||
            copy.polygon_at(1).color().r != 30 ||
            copy.polygon_limit() != 3)
        {
            std::cerr << "polygon collection copy or removal changed its contents\n";
            return false;
        }

        poly_paint::PolygonCollection moved = std::move(copy);
        if (!moved.add(make_polygon(40)) || !moved.full())
        {
            std::cerr << "moved polygon collection lost its capacity or contents\n";
            return false;
        }
        moved.clear();
        if (!moved.empty() || moved.polygon_limit() != 3)
        {
            std::cerr << "clearing a polygon collection changed its selected limit\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool test_frequency_weighted_target_colors()
    {
        constexpr std::array<std::uint8_t, 4 * poly_paint::rgba_channel_count> pixels {
            30, 90, 40, 0,
            30, 90, 40, 255,
            30, 90, 40, 128,
            100, 70, 30, 255};

        std::mt19937 random_engine(42);
        const poly_paint::PolygonCollection collection =
            poly_paint::make_random_polygon_collection(random_engine, 1'000, pixels);
        std::size_t green_count = 0;
        std::size_t brown_count = 0;
        for (const poly_paint::Polygon& polygon : collection)
        {
            const poly_paint::RgbaColor color = polygon.color();
            if (color.r == 30 && color.g == 90 && color.b == 40)
            {
                ++green_count;
            }
            else if (color.r == 100 && color.g == 70 && color.b == 30)
            {
                ++brown_count;
            }
        }

        const std::size_t target_color_count = green_count + brown_count;
        if (target_color_count < 650 || target_color_count > 850 ||
            green_count <= brown_count * 2)
        {
            std::cerr << "new polygon colors were not biased by target pixel frequency\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool test_cooperative_runner()
    {
        constexpr std::size_t width = 2;
        constexpr std::size_t height = 2;
        constexpr std::array<std::uint8_t, width * height * poly_paint::rgba_channel_count> target {
            0, 0, 0, 255,
            255, 255, 255, 255,
            255, 0, 0, 255,
            0, 0, 255, 255};
        const poly_paint::ImageSimilarityScorer scorer(width, height, target);
        poly_paint::EvolutionRunSettings settings;
        settings.maximum_generations = 3;
        settings.polygon_count = 1;
        settings.parent_count = 1;
        settings.offspring_count = 1;
        settings.island_count = 3;

        poly_paint::EvolutionRunner runner;
        runner.start(scorer, settings);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!runner.pause_requested() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (!runner.pause_requested() || !runner.running())
        {
            runner.stop_and_wait();
            std::cerr << "generation limit did not pause the cooperative run\n";
            return false;
        }
        const std::optional<poly_paint::EvolutionUpdate> update = runner.take_latest_update();
        if (!update || update->generation != settings.maximum_generations)
        {
            runner.stop_and_wait();
            std::cerr << "generation-limit pause did not publish the limiting generation\n";
            return false;
        }
        if (scorer.score(update->rgba, width, height) != update->score)
        {
            runner.stop_and_wait();
            std::cerr << "the published global-best image and score did not match\n";
            return false;
        }

        runner.resume();
        const auto resume_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::optional<poly_paint::EvolutionUpdate> resumed_update;
        while (std::chrono::steady_clock::now() < resume_deadline)
        {
            if (std::optional next = runner.take_latest_update();
                next && next->generation > settings.maximum_generations)
            {
                resumed_update = std::move(next);
                break;
            }
            std::this_thread::yield();
        }
        runner.stop_and_wait();
        if (!resumed_update)
        {
            std::cerr << "cooperative evolution did not continue after resuming\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool run_tests()
    {
        if (!test_highway_fill_and_difference_kernels())
        {
            return false;
        }
        if (!test_opaque_blend_kernel())
        {
            return false;
        }
        if (!test_evolution_migration())
        {
            return false;
        }
        if (!test_polygon_collection_storage())
        {
            return false;
        }
        if (!test_frequency_weighted_target_colors())
        {
            return false;
        }
        if (!test_cooperative_runner())
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
