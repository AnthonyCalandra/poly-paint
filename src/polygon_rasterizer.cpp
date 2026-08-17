#include "polygon_rasterizer.h"

#include "image_dimensions.h"
#include "pixel_kernels.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace poly_paint
{
    namespace
    {
        constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();

        struct PixelSpan
        {
            std::size_t first {};
            std::size_t count {};

            [[nodiscard]] std::size_t end() const noexcept
            {
                return first + count;
            }
        };

        [[nodiscard]] std::optional<PixelSpan> normalized_span_to_pixels(
            float normalized_start,
            float normalized_end,
            std::size_t extent)
        {
            assert(std::isfinite(normalized_start));
            assert(std::isfinite(normalized_end));
            assert(extent > 0);

            if (normalized_start >= normalized_end ||
                normalized_end <= 0.0f || normalized_start >= 1.0f)
            {
                return std::nullopt;
            }

            const double pixel_extent = static_cast<double>(extent);
            const double first = std::clamp(
                std::ceil(static_cast<double>(std::max(0.0f, normalized_start)) *
                    pixel_extent - 0.5),
                0.0,
                pixel_extent);
            const double end = std::clamp(
                std::ceil(static_cast<double>(std::min(1.0f, normalized_end)) *
                    pixel_extent - 0.5),
                0.0,
                pixel_extent);
            if (first >= end)
            {
                return std::nullopt;
            }

            const std::size_t first_pixel = static_cast<std::size_t>(first);
            const std::size_t end_pixel = static_cast<std::size_t>(end);
            return PixelSpan {first_pixel, end_pixel - first_pixel};
        }

        void require_buffer_size(std::span<const std::uint8_t> rgba, std::size_t expected_size)
        {
            if (rgba.size() != expected_size)
            {
                throw std::invalid_argument("RGBA output buffer does not match the requested raster dimensions.");
            }
        }

        void blend_source_over(
            std::span<std::uint8_t, rgba_channel_count> destination,
            RgbaColor source)
        {
            const std::uint32_t source_alpha = source.a;
            const std::uint32_t destination_alpha = destination[alpha_channel_index];
            const std::uint32_t inverse_source_alpha = maximum_channel - source_alpha;
            const std::uint32_t output_alpha = source_alpha +
                (destination_alpha * inverse_source_alpha + maximum_channel / 2) / maximum_channel;

            if (output_alpha == 0)
            {
                std::fill(destination.begin(), destination.end(), std::uint8_t {});
                return;
            }

            const std::array source_channels {source.r, source.g, source.b};
            for (std::size_t channel = 0; channel < source_channels.size(); ++channel)
            {
                const std::uint32_t premultiplied_output =
                    source_channels[channel] * source_alpha * maximum_channel +
                    destination[channel] * destination_alpha * inverse_source_alpha;
                destination[channel] = static_cast<std::uint8_t>(
                    (premultiplied_output + output_alpha * maximum_channel / 2) /
                    (output_alpha * maximum_channel));
            }
            destination[alpha_channel_index] = static_cast<std::uint8_t>(output_alpha);
        }

        void blend_source_over_span(
            std::span<std::uint8_t> destination,
            RgbaColor color)
        {
            for (std::size_t offset = 0; offset < destination.size(); offset += rgba_channel_count)
            {
                blend_source_over(
                    std::span<std::uint8_t, rgba_channel_count> {
                        destination.data() + offset,
                        rgba_channel_count
                    },
                    color);
            }
        }

        void rasterize_polygon(
            const Polygon& polygon,
            std::size_t width,
            std::size_t image_height,
            std::size_t first_row,
            std::size_t row_count,
            std::span<std::uint8_t> output_rgba)
        {
            const std::span<const PolygonPoint> vertices = polygon.vertices();
            float min_x = vertices.front().x;
            float max_x = min_x;
            float min_y = vertices.front().y;
            float max_y = min_y;
            for (const PolygonPoint& vertex : vertices)
            {
                assert(std::isfinite(vertex.x));
                assert(std::isfinite(vertex.y));
                min_x = std::min(min_x, vertex.x);
                max_x = std::max(max_x, vertex.x);
                min_y = std::min(min_y, vertex.y);
                max_y = std::max(max_y, vertex.y);
            }

            const std::optional<PixelSpan> polygon_rows =
                normalized_span_to_pixels(min_y, max_y, image_height);
            if (!polygon_rows || max_x < 0.0f || min_x > 1.0f)
            {
                return;
            }

            const std::size_t first_y = std::max(polygon_rows->first, first_row);
            const std::size_t end_y = std::min(polygon_rows->end(), first_row + row_count);
            if (first_y >= end_y)
            {
                return;
            }

            const float image_height_float = static_cast<float>(image_height);
            constexpr std::size_t opaque_span_batch_capacity = 128;
            std::array<detail::MutableRgbaSpan, opaque_span_batch_capacity> opaque_spans {};
            std::size_t opaque_span_count = 0;
            const RgbaColor color = polygon.color();
            const auto flush_opaque_spans = [&]
            {
                if (opaque_span_count == 0)
                {
                    return;
                }
                detail::blend_source_over_opaque(
                    std::span<const detail::MutableRgbaSpan> {
                        opaque_spans.data(), opaque_span_count},
                    color);
                opaque_span_count = 0;
            };
            std::array<float, Polygon::max_vertices> intersections {};
            for (std::size_t y = first_y; y < end_y; ++y)
            {
                const float sample_y =
                    (static_cast<float>(y) + 0.5f) / image_height_float;
                std::size_t intersection_count = 0;
                std::size_t previous = vertices.size() - 1;
                for (std::size_t current = 0; current < vertices.size(); ++current)
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

                std::ranges::sort(std::span {intersections}.first(intersection_count));
                for (std::size_t pair = 0; pair + 1 < intersection_count; pair += 2)
                {
                    const std::optional<PixelSpan> pixel_span = normalized_span_to_pixels(
                        intersections[pair], intersections[pair + 1], width);
                    if (!pixel_span)
                    {
                        continue;
                    }

                    const std::size_t first_byte =
                        ((y - first_row) * width + pixel_span->first) * rgba_channel_count;
                    const std::size_t byte_count = pixel_span->count * rgba_channel_count;
                    const std::span<std::uint8_t> destination =
                        output_rgba.subspan(first_byte, byte_count);
                    if (color.a == maximum_channel)
                    {
                        detail::fill_rgba_pixels(destination, color);
                    }
                    else if (destination[alpha_channel_index] == maximum_channel)
                    {
                        opaque_spans[opaque_span_count++] = destination;
                        if (opaque_span_count == opaque_spans.size())
                        {
                            flush_opaque_spans();
                        }
                    }
                    else
                    {
                        blend_source_over_span(destination, color);
                    }
                }
            }
            flush_opaque_spans();
        }
    }

    std::vector<std::uint8_t> rasterize(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        RgbaColor background)
    {
        if (width == 0 || height == 0)
        {
            throw std::invalid_argument("Rasterization requires positive image dimensions.");
        }
        std::vector<std::uint8_t> rgba(checked_rgba_byte_count(width, height));
        rasterize_into(polygons, width, height, rgba, background);
        return rgba;
    }

    void rasterize_into(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t height,
        std::span<std::uint8_t> output_rgba,
        RgbaColor background)
    {
        rasterize_rows_into(polygons, width, height, 0, height, output_rgba, background);
    }

    void rasterize_rows_into(
        const PolygonCollection& polygons,
        std::size_t width,
        std::size_t image_height,
        std::size_t first_row,
        std::size_t row_count,
        std::span<std::uint8_t> output_rgba,
        RgbaColor background)
    {
        if (width == 0 || image_height == 0 || row_count == 0 ||
            row_count > image_height || first_row > image_height - row_count)
        {
            throw std::invalid_argument("Rasterization requires a valid image-row range.");
        }
        require_buffer_size(output_rgba, checked_rgba_byte_count(width, row_count));
        detail::fill_rgba_pixels(output_rgba, background);
        for (const Polygon& polygon : polygons.polygons())
        {
            rasterize_polygon(polygon, width, image_height, first_row, row_count, output_rgba);
        }
    }
}
