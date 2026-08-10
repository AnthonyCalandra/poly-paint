#pragma once

#include "polygon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <stdexcept>
#include <vector>

namespace poly_paint
{
    // Converts a polygon genome into a row-major RGBA image. Polygons are drawn
    // in collection order: later polygons are composited over earlier ones.
    class PolygonRasterizer
    {
    public:
        [[nodiscard]] static std::vector<std::uint8_t> rasterize(
            const PolygonCollection& polygons,
            int width,
            int height,
            RgbaColor background = {0, 0, 0, 255})
        {
            if (width <= 0 || height <= 0)
            {
                throw std::invalid_argument("Rasterization requires positive image dimensions.");
            }

            std::vector<std::uint8_t> rgba(rgba_byte_count(width, height));
            rasterize_into(polygons, width, height, rgba.data(), background);
            return rgba;
        }

        static void rasterize_into(
            const PolygonCollection& polygons,
            int width,
            int height,
            std::uint8_t* output_rgba,
            RgbaColor background = {0, 0, 0, 255})
        {
            if (width <= 0 || height <= 0 || output_rgba == nullptr)
            {
                throw std::invalid_argument("Rasterization requires a valid RGBA output buffer.");
            }

            rasterize_rows_into(polygons, width, height, 0, height, output_rgba, background);
        }

        // Rasterizes a contiguous set of image rows into a compact output buffer.
        // This lets fitness evaluation reuse a small cache-friendly tile rather
        // than allocating a complete candidate image for every worker.
        static void rasterize_rows_into(
            const PolygonCollection& polygons,
            int width,
            int image_height,
            int first_row,
            int row_count,
            std::uint8_t* output_rgba,
            RgbaColor background = {0, 0, 0, 255})
        {
            if (width <= 0 || image_height <= 0 || first_row < 0 || row_count <= 0 ||
                first_row > image_height - row_count || output_rgba == nullptr)
            {
                throw std::invalid_argument("Rasterization requires a valid image-row range and RGBA output buffer.");
            }

            fill_background(output_rgba, rgba_byte_count(width, row_count), background);
            for (const Polygon& polygon : polygons)
            {
                rasterize_polygon(polygon, width, image_height, first_row, row_count, output_rgba);
            }
        }

    private:
        static std::size_t rgba_byte_count(int width, int height)
        {
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        }

        static void fill_background(std::uint8_t* output_rgba, std::size_t byte_count, RgbaColor background)
        {
            fill_rgba_pixels(output_rgba, byte_count / 4, background);
        }

        static void fill_rgba_pixels(std::uint8_t* output_rgba, std::size_t pixel_count, RgbaColor color)
        {
            const std::uint32_t packed_color = static_cast<std::uint32_t>(color.r) |
                (static_cast<std::uint32_t>(color.g) << 8) |
                (static_cast<std::uint32_t>(color.b) << 16) |
                (static_cast<std::uint32_t>(color.a) << 24);
            const __m256i vector_color = _mm256_set1_epi32(static_cast<int>(packed_color));

            std::size_t pixel = 0;
            for (; pixel + 8 <= pixel_count; pixel += 8)
            {
                _mm256_storeu_si256(
                    reinterpret_cast<__m256i*>(output_rgba + pixel * 4), vector_color);
            }
            for (; pixel < pixel_count; ++pixel)
            {
                std::uint8_t* destination = output_rgba + pixel * 4;
                destination[0] = color.r;
                destination[1] = color.g;
                destination[2] = color.b;
                destination[3] = color.a;
            }
        }

        static void rasterize_polygon(
            const Polygon& polygon,
            int width,
            int image_height,
            int first_row,
            int row_count,
            std::uint8_t* output_rgba)
        {
            const Polygon::VertexStorage& vertices = polygon.vertices();
            const std::size_t vertex_count = polygon.vertex_count();
            float min_x = vertices.front().x;
            float max_x = min_x;
            float min_y = vertices.front().y;
            float max_y = min_y;
            for (std::size_t index = 0; index < vertex_count; ++index)
            {
                const PolygonPoint& vertex = vertices[index];
                min_x = std::min(min_x, vertex.x);
                max_x = std::max(max_x, vertex.x);
                min_y = std::min(min_y, vertex.y);
                max_y = std::max(max_y, vertex.y);
            }

            const int unclamped_first_y = static_cast<int>(std::ceil(min_y * image_height - 0.5f));
            const int unclamped_last_y = static_cast<int>(std::ceil(max_y * image_height - 0.5f)) - 1;
            const int tile_last_row = first_row + row_count - 1;
            if (unclamped_last_y < first_row || unclamped_first_y > tile_last_row ||
                max_x < 0.0f || min_x > 1.0f)
            {
                return;
            }
            const int first_y = std::max(unclamped_first_y, first_row);
            const int last_y = std::min(unclamped_last_y, tile_last_row);

            std::array<float, Polygon::max_vertices> intersections {};
            for (int y = first_y; y <= last_y; ++y)
            {
                const float sample_y = (static_cast<float>(y) + 0.5f) / static_cast<float>(image_height);
                std::size_t intersection_count = 0;
                std::size_t previous = vertex_count - 1;
                for (std::size_t current = 0; current < vertex_count; ++current)
                {
                    const PolygonPoint& a = vertices[current];
                    const PolygonPoint& b = vertices[previous];
                    if ((a.y > sample_y) != (b.y > sample_y))
                    {
                        intersections[intersection_count++] =
                            (b.x - a.x) * (sample_y - a.y) / (b.y - a.y) + a.x;
                    }
                    previous = current;
                }

                std::sort(intersections.begin(), intersections.begin() + static_cast<std::ptrdiff_t>(intersection_count));
                for (std::size_t pair = 0; pair + 1 < intersection_count; pair += 2)
                {
                    if (intersections[pair + 1] <= 0.0f || intersections[pair] >= 1.0f)
                    {
                        continue;
                    }
                    const float span_start = std::max(0.0f, intersections[pair]);
                    const float span_end = std::min(1.0f, intersections[pair + 1]);
                    const int first_x = std::clamp(
                        static_cast<int>(std::ceil(span_start * width - 0.5f)), 0, width - 1);
                    const int last_x = std::clamp(
                        static_cast<int>(std::ceil(span_end * width - 0.5f)) - 1,
                        0,
                        width - 1);
                    if (first_x > last_x)
                    {
                        continue;
                    }

                    const RgbaColor color = polygon.color();
                    const std::size_t pixel =
                        (static_cast<std::size_t>(y - first_row) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(first_x)) * 4;
                    const std::size_t pixel_count = static_cast<std::size_t>(last_x - first_x + 1);
                    if (color.a == UINT8_MAX)
                    {
                        fill_rgba_pixels(output_rgba + pixel, pixel_count, color);
                    }
                    else
                    {
                        for (std::size_t offset = 0; offset < pixel_count; ++offset)
                        {
                            blend_source_over(output_rgba + pixel + offset * 4, color);
                        }
                    }
                }
            }
        }

        static void blend_source_over(std::uint8_t* destination, RgbaColor source)
        {
            const std::uint32_t source_alpha = source.a;
            const std::uint32_t destination_alpha = destination[3];
            const std::uint32_t inverse_source_alpha = UINT8_MAX - source_alpha;
            const std::uint32_t output_alpha = source_alpha +
                (destination_alpha * inverse_source_alpha + UINT8_MAX / 2) / UINT8_MAX;

            if (output_alpha == 0)
            {
                destination[0] = 0;
                destination[1] = 0;
                destination[2] = 0;
                destination[3] = 0;
                return;
            }

            const std::uint8_t source_channels[] {source.r, source.g, source.b};
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const std::uint32_t premultiplied_output =
                    static_cast<std::uint32_t>(source_channels[channel]) * source_alpha * UINT8_MAX +
                    static_cast<std::uint32_t>(destination[channel]) * destination_alpha * inverse_source_alpha;
                destination[channel] = static_cast<std::uint8_t>(
                    (premultiplied_output + output_alpha * UINT8_MAX / 2) /
                    (output_alpha * UINT8_MAX));
            }
            destination[3] = static_cast<std::uint8_t>(output_alpha);
        }
    };
}
