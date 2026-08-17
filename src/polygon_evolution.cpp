#include "polygon_evolution.h"

#include "image_dimensions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

namespace poly_paint
{
    namespace
    {
        constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();

        enum class MutationKind : unsigned int
        {
            color,
            vertex,
            translation,
            scale
        };

        constexpr std::array mutation_kinds {
            MutationKind::color,
            MutationKind::vertex,
            MutationKind::translation,
            MutationKind::scale
        };

        [[nodiscard]] Polygon make_random_polygon(std::mt19937& random_engine)
        {
            std::uniform_real_distribution<float> coordinate(-0.15f, 1.15f);
            std::uniform_int_distribution<unsigned int> color_channel(0, maximum_channel);
            std::uniform_int_distribution<unsigned int> alpha_channel(30, 60);
            std::uniform_int_distribution<std::size_t> vertex_count(3, 7);

            const std::size_t count = vertex_count(random_engine);
            Polygon::VertexStorage vertices {};
            for (std::size_t index = 0; index < count; ++index)
            {
                vertices[index] = {coordinate(random_engine), coordinate(random_engine)};
            }
            return Polygon(
                std::span<const PolygonPoint> {vertices}.first(count),
                {
                    static_cast<std::uint8_t>(color_channel(random_engine)),
                    static_cast<std::uint8_t>(color_channel(random_engine)),
                    static_cast<std::uint8_t>(color_channel(random_engine)),
                    static_cast<std::uint8_t>(alpha_channel(random_engine))
                });
        }

        [[nodiscard]] PolygonPoint centroid_of(const Polygon& polygon)
        {
            PolygonPoint centroid {};
            for (const PolygonPoint& vertex : polygon.vertices())
            {
                centroid.x += vertex.x;
                centroid.y += vertex.y;
            }
            const float inverse_count = 1.0f / static_cast<float>(polygon.vertices().size());
            centroid.x *= inverse_count;
            centroid.y *= inverse_count;
            return centroid;
        }
    }

    PolygonCollection make_random_polygon_collection(
        std::mt19937& random_engine,
        std::size_t polygon_count)
    {
        PolygonCollection collection(polygon_count);
        while (!collection.full())
        {
            collection.add(make_random_polygon(random_engine));
        }
        return collection;
    }

    std::vector<ContrastSeed> find_high_contrast_seeds(
        std::span<const std::uint8_t> rgba,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count)
    {
        if (width == 0 || height == 0 || polygon_count == 0 ||
            rgba.size() != checked_rgba_byte_count(width, height))
        {
            return {};
        }

        const std::size_t sample_radius = std::clamp(
            std::min(width, height) / 64, std::size_t {2}, std::size_t {16});
        const std::size_t sample_stride = std::max(std::size_t {1}, sample_radius / 2);
        std::vector<ContrastSeed> candidates;
        if (width <= sample_radius * 2 || height <= sample_radius * 2)
        {
            return {};
        }
        candidates.reserve(checked_pixel_count(width / sample_stride, height / sample_stride));

        const auto color_at = [rgba, width](
            std::size_t x,
            std::size_t y,
            std::size_t channel)
        {
            return rgba[(y * width + x) * rgba_channel_count + channel];
        };
        for (std::size_t y = sample_radius; y < height - sample_radius; y += sample_stride)
        {
            for (std::size_t x = sample_radius; x < width - sample_radius; x += sample_stride)
            {
                int contrast = 0;
                for (std::size_t channel = 0; channel < rgb_channel_count; ++channel)
                {
                    const int surrounding_average =
                        (color_at(x - sample_radius, y, channel) +
                         color_at(x + sample_radius, y, channel) +
                         color_at(x, y - sample_radius, channel) +
                         color_at(x, y + sample_radius, channel)) / 4;
                    contrast += std::abs(color_at(x, y, channel) - surrounding_average);
                }
                candidates.push_back({
                    x,
                    y,
                    {color_at(x, y, 0), color_at(x, y, 1), color_at(x, y, 2), maximum_channel},
                    static_cast<std::uint16_t>(contrast)
                });
            }
        }

        std::ranges::sort(candidates, {}, &ContrastSeed::contrast);
        std::ranges::reverse(candidates);

        std::vector<ContrastSeed> seeds;
        seeds.reserve(polygon_count);
        const float minimum_separation = 0.55f * std::sqrt(
            static_cast<float>(width) * static_cast<float>(height) / static_cast<float>(polygon_count));
        const float minimum_separation_squared = minimum_separation * minimum_separation;
        for (const ContrastSeed& candidate : candidates)
        {
            const bool sufficiently_separated = std::ranges::all_of(seeds, [&](const ContrastSeed& selected)
            {
                const float delta_x =
                    static_cast<float>(candidate.x) - static_cast<float>(selected.x);
                const float delta_y =
                    static_cast<float>(candidate.y) - static_cast<float>(selected.y);
                return delta_x * delta_x + delta_y * delta_y >= minimum_separation_squared;
            });
            if (sufficiently_separated)
            {
                seeds.push_back(candidate);
                if (seeds.size() == polygon_count)
                {
                    return seeds;
                }
            }
        }

        for (const ContrastSeed& candidate : candidates)
        {
            if (seeds.size() == polygon_count)
            {
                break;
            }
            const bool selected = std::ranges::any_of(seeds, [&](const ContrastSeed& seed)
            {
                return seed.x == candidate.x && seed.y == candidate.y;
            });
            if (!selected)
            {
                seeds.push_back(candidate);
            }
        }
        return seeds;
    }

    PolygonCollection make_best_guess_polygon_collection(
        std::span<const ContrastSeed> seeds,
        std::size_t width,
        std::size_t height,
        std::size_t polygon_count,
        std::mt19937& random_engine)
    {
        PolygonCollection collection(polygon_count);
        std::uniform_int_distribution<unsigned int> alpha_channel(30, 60);
        for (const ContrastSeed& seed : seeds)
        {
            const float area_per_polygon = static_cast<float>(width) * static_cast<float>(height) /
                static_cast<float>(polygon_count);
            const float base_radius = std::sqrt(area_per_polygon / 2.6f);
            std::normal_distribution<float> position_jitter(0.0f, base_radius * 0.10f);
            std::normal_distribution<float> scale_jitter(1.0f, 0.12f);
            const float center_x = static_cast<float>(seed.x) + position_jitter(random_engine);
            const float center_y = static_cast<float>(seed.y) + position_jitter(random_engine);
            const float radius = base_radius * std::clamp(scale_jitter(random_engine), 0.65f, 1.35f);

            constexpr std::size_t vertex_count = 6;
            Polygon::VertexStorage vertices {};
            for (std::size_t index = 0; index < vertex_count; ++index)
            {
                constexpr float pi = 3.14159265358979323846f;
                const float angle = (2.0f * pi * static_cast<float>(index)) / static_cast<float>(vertex_count);
                vertices[index] = {
                    (center_x + std::cos(angle) * radius) / static_cast<float>(width),
                    (center_y + std::sin(angle) * radius) / static_cast<float>(height)
                };
            }
            collection.add(Polygon(
                std::span<const PolygonPoint> {vertices}.first(vertex_count),
                {seed.color.r, seed.color.g, seed.color.b,
                 static_cast<std::uint8_t>(alpha_channel(random_engine))}));
        }
        while (!collection.full())
        {
            collection.add(make_random_polygon(random_engine));
        }
        return collection;
    }

    PolygonCollection mutate_polygon_collection(
        const PolygonCollection& parent,
        std::mt19937& random_engine)
    {
        PolygonCollection child = parent;
        std::uniform_int_distribution<std::size_t> polygon_index(0, child.size() - 1);
        const std::size_t selected_polygon = polygon_index(random_engine);
        std::uniform_int_distribution<unsigned int> mutation_tier(0, 99);
        const unsigned int tier = mutation_tier(random_engine);
        if (tier >= 95)
        {
            child.replace(selected_polygon, make_random_polygon(random_engine));
            return child;
        }

        Polygon updated = child.polygon_at(selected_polygon);
        std::uniform_int_distribution<unsigned int> teleport_roll(0, 99);
        if (teleport_roll(random_engine) < 10)
        {
            const PolygonPoint centroid = centroid_of(updated);
            std::uniform_real_distribution<float> canvas_position(0.0f, 1.0f);
            const float offset_x = canvas_position(random_engine) - centroid.x;
            const float offset_y = canvas_position(random_engine) - centroid.y;
            for (std::size_t index = 0; index < updated.vertex_count(); ++index)
            {
                PolygonPoint vertex = updated.vertex_at(index);
                vertex.x += offset_x;
                vertex.y += offset_y;
                updated.set_vertex(index, vertex);
            }
            child.replace(selected_polygon, std::move(updated));
            return child;
        }

        std::uniform_int_distribution<std::size_t> mutation_index(0, mutation_kinds.size() - 1);
        const bool medium_mutation = tier >= 70;
        switch (mutation_kinds[mutation_index(random_engine)])
        {
        case MutationKind::color:
        {
            RgbaColor color = updated.color();
            std::uniform_int_distribution<std::size_t> channel(0, rgba_channel_count - 1);
            std::normal_distribution<float> color_change(0.0f, medium_mutation ? 64.0f : 16.0f);
            std::uniform_int_distribution<unsigned int> alpha_channel(30, 60);
            std::array<std::uint8_t*, rgba_channel_count> channels {
                &color.r, &color.g, &color.b, &color.a};
            const std::size_t selected_channel = channel(random_engine);
            if (selected_channel == alpha_channel_index)
            {
                color.a = static_cast<std::uint8_t>(alpha_channel(random_engine));
            }
            else
            {
                const int changed = std::clamp(
                    static_cast<int>(*channels[selected_channel] + color_change(random_engine)),
                    0,
                    static_cast<int>(maximum_channel));
                *channels[selected_channel] = static_cast<std::uint8_t>(changed);
            }
            updated.set_color(color);
            break;
        }
        case MutationKind::vertex:
        {
            std::uniform_int_distribution<std::size_t> vertex_index(0, updated.vertex_count() - 1);
            std::normal_distribution<float> position_change(0.0f, medium_mutation ? 0.16f : 0.04f);
            const std::size_t selected_vertex = vertex_index(random_engine);
            PolygonPoint vertex = updated.vertex_at(selected_vertex);
            vertex.x = std::clamp(vertex.x + position_change(random_engine), -0.25f, 1.25f);
            vertex.y = std::clamp(vertex.y + position_change(random_engine), -0.25f, 1.25f);
            updated.set_vertex(selected_vertex, vertex);
            break;
        }
        case MutationKind::translation:
        {
            std::normal_distribution<float> translation(0.0f, medium_mutation ? 0.16f : 0.04f);
            const float offset_x = translation(random_engine);
            const float offset_y = translation(random_engine);
            for (std::size_t index = 0; index < updated.vertex_count(); ++index)
            {
                PolygonPoint vertex = updated.vertex_at(index);
                vertex.x = std::clamp(vertex.x + offset_x, -0.25f, 1.25f);
                vertex.y = std::clamp(vertex.y + offset_y, -0.25f, 1.25f);
                updated.set_vertex(index, vertex);
            }
            break;
        }
        case MutationKind::scale:
        {
            const PolygonPoint centroid = centroid_of(updated);
            std::normal_distribution<float> scale_change(1.0f, medium_mutation ? 0.50f : 0.15f);
            const float scale = std::clamp(scale_change(random_engine), 0.25f, 2.5f);
            for (std::size_t index = 0; index < updated.vertex_count(); ++index)
            {
                PolygonPoint vertex = updated.vertex_at(index);
                vertex.x = std::clamp(centroid.x + (vertex.x - centroid.x) * scale, -0.25f, 1.25f);
                vertex.y = std::clamp(centroid.y + (vertex.y - centroid.y) * scale, -0.25f, 1.25f);
                updated.set_vertex(index, vertex);
            }
            break;
        }
        }

        child.replace(selected_polygon, std::move(updated));
        return child;
    }
}
