#include "pixel_kernels.h"

#include "image_dimensions.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace poly_paint::detail
{
    namespace
    {
        constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();

        void validate_rgba(std::span<const std::uint8_t> rgba)
        {
            if (rgba.size() % rgba_channel_count != 0)
            {
                throw std::invalid_argument("RGBA buffers must contain complete pixels.");
            }
        }
    }

    void fill_rgba_pixels(std::span<std::uint8_t> rgba, RgbaColor color)
    {
        validate_rgba(rgba);
        for (std::size_t offset = 0; offset < rgba.size(); offset += rgba_channel_count)
        {
            rgba[offset] = color.r;
            rgba[offset + 1] = color.g;
            rgba[offset + 2] = color.b;
            rgba[offset + alpha_channel_index] = color.a;
        }
    }

    void blend_source_over_opaque(std::span<std::uint8_t> rgba, RgbaColor source)
    {
        validate_rgba(rgba);
        const std::uint32_t alpha = source.a;
        const std::uint32_t inverse_alpha = maximum_channel - alpha;
        const std::array source_channels {source.r, source.g, source.b};
        for (std::size_t offset = 0; offset < rgba.size(); offset += rgba_channel_count)
        {
            for (std::size_t channel = 0; channel < source_channels.size(); ++channel)
            {
                std::uint32_t sum = static_cast<std::uint32_t>(source_channels[channel]) * alpha +
                    static_cast<std::uint32_t>(rgba[offset + channel]) * inverse_alpha + 128;
                sum += sum >> 8;
                rgba[offset + channel] = static_cast<std::uint8_t>(sum >> 8);
            }
            rgba[offset + alpha_channel_index] = maximum_channel;
        }
    }

    std::uint64_t rgb_absolute_difference(
        std::span<const std::uint8_t> left,
        std::span<const std::uint8_t> right)
    {
        validate_rgba(left);
        if (left.size() != right.size())
        {
            throw std::invalid_argument("Image comparison buffers must have equal sizes.");
        }

        std::uint64_t total = 0;
        for (std::size_t offset = 0; offset < left.size(); offset += rgba_channel_count)
        {
            for (std::size_t channel = 0; channel < rgb_channel_count; ++channel)
            {
                const int difference = static_cast<int>(left[offset + channel]) -
                    static_cast<int>(right[offset + channel]);
                total += static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
            }
        }
        return total;
    }
}
