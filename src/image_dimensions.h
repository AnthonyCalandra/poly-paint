#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace poly_paint
{
    /** @brief Number of color channels in an RGB pixel. */
    inline constexpr std::size_t rgb_channel_count = 3;
    /** @brief Number of channels in an RGBA pixel. */
    inline constexpr std::size_t rgba_channel_count = 4;
    /** @brief Zero-based index of the alpha channel in an RGBA pixel. */
    inline constexpr std::size_t alpha_channel_index = rgb_channel_count;

    /** @brief Returns @p width times @p height, throwing on overflow. */
    [[nodiscard]] inline std::size_t checked_pixel_count(
        std::size_t width,
        std::size_t height)
    {
        if (width != 0 && height > std::numeric_limits<std::size_t>::max() / width)
        {
            throw std::overflow_error("Image dimensions exceed the addressable pixel count.");
        }
        return width * height;
    }

    /** @brief Returns the required RGBA byte count, throwing on overflow. */
    [[nodiscard]] inline std::size_t checked_rgba_byte_count(
        std::size_t width,
        std::size_t height)
    {
        const std::size_t pixels = checked_pixel_count(width, height);
        if (pixels > std::numeric_limits<std::size_t>::max() / rgba_channel_count)
        {
            throw std::overflow_error("Image dimensions exceed the addressable RGBA buffer size.");
        }
        return pixels * rgba_channel_count;
    }

    /** @brief Converts an integral value to @p Destination, throwing if it cannot fit. */
    template <std::integral Destination, std::integral Source>
    [[nodiscard]] Destination checked_narrow(Source value, std::string_view description)
    {
        if (!std::in_range<Destination>(value))
        {
            throw std::overflow_error(std::string {description} + " is outside the supported range.");
        }
        return static_cast<Destination>(value);
    }
}
