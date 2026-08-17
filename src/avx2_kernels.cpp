#include "pixel_kernels.h"

#include "image_dimensions.h"

#include <immintrin.h>

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
        constexpr std::uint32_t packed_rgb_mask =
            static_cast<std::uint32_t>(maximum_channel) |
            (static_cast<std::uint32_t>(maximum_channel) << 8) |
            (static_cast<std::uint32_t>(maximum_channel) << 16);

        [[nodiscard]] std::uint32_t pack_color(RgbaColor color) noexcept
        {
            return static_cast<std::uint32_t>(color.r) |
                (static_cast<std::uint32_t>(color.g) << 8) |
                (static_cast<std::uint32_t>(color.b) << 16) |
                (static_cast<std::uint32_t>(color.a) << 24);
        }

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
        const __m256i vector_color = _mm256_set1_epi32(static_cast<int>(pack_color(color)));

        std::size_t offset = 0;
        for (; offset + 32 <= rgba.size(); offset += 32)
        {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(rgba.data() + offset), vector_color);
        }
        for (; offset < rgba.size(); offset += rgba_channel_count)
        {
            rgba[offset] = color.r;
            rgba[offset + 1] = color.g;
            rgba[offset + 2] = color.b;
            rgba[offset + alpha_channel_index] = color.a;
        }
    }

    void blend_source_over_opaque(
        std::span<const MutableRgbaSpan> rgba_spans,
        RgbaColor source)
    {
        for (const MutableRgbaSpan rgba : rgba_spans)
        {
            validate_rgba(rgba);
        }

        const __m256i zero = _mm256_setzero_si256();
        const __m256i source_color = _mm256_set1_epi32(static_cast<int>(pack_color(source)));
        const __m256i source_alpha = _mm256_set1_epi16(static_cast<short>(source.a));
        const __m256i inverse_source_alpha = _mm256_set1_epi16(
            static_cast<short>(maximum_channel - source.a));
        const __m256i rounding = _mm256_set1_epi16(128);
        const __m256i source_channels = _mm256_unpacklo_epi8(source_color, zero);
        const __m256i source_terms = _mm256_add_epi16(
            _mm256_mullo_epi16(source_channels, source_alpha), rounding);
        const auto opaque_alpha = static_cast<std::uint32_t>(maximum_channel) << 24;
        const __m256i opaque_alpha_mask = _mm256_set1_epi32(static_cast<int>(opaque_alpha));
        const std::uint32_t alpha = source.a;
        const std::uint32_t inverse_alpha = maximum_channel - alpha;
        const std::uint32_t red_source_term =
            static_cast<std::uint32_t>(source.r) * alpha + 128;
        const std::uint32_t green_source_term =
            static_cast<std::uint32_t>(source.g) * alpha + 128;
        const std::uint32_t blue_source_term =
            static_cast<std::uint32_t>(source.b) * alpha + 128;

        for (const MutableRgbaSpan rgba : rgba_spans)
        {
            std::size_t offset = 0;
            for (; offset + 32 <= rgba.size(); offset += 32)
            {
                const __m256i destination = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(rgba.data() + offset));
                __m256i blended_low = _mm256_add_epi16(
                    source_terms,
                    _mm256_mullo_epi16(
                        _mm256_unpacklo_epi8(destination, zero), inverse_source_alpha));
                blended_low = _mm256_add_epi16(
                    blended_low, _mm256_srli_epi16(blended_low, 8));
                blended_low = _mm256_srli_epi16(blended_low, 8);

                __m256i blended_high = _mm256_add_epi16(
                    source_terms,
                    _mm256_mullo_epi16(
                        _mm256_unpackhi_epi8(destination, zero), inverse_source_alpha));
                blended_high = _mm256_add_epi16(
                    blended_high, _mm256_srli_epi16(blended_high, 8));
                blended_high = _mm256_srli_epi16(blended_high, 8);

                const __m256i blended = _mm256_or_si256(
                    _mm256_packus_epi16(blended_low, blended_high), opaque_alpha_mask);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(rgba.data() + offset), blended);
            }

            for (; offset < rgba.size(); offset += rgba_channel_count)
            {
                std::uint32_t red = red_source_term +
                    static_cast<std::uint32_t>(rgba[offset]) * inverse_alpha;
                red += red >> 8;
                rgba[offset] = static_cast<std::uint8_t>(red >> 8);

                std::uint32_t green = green_source_term +
                    static_cast<std::uint32_t>(rgba[offset + 1]) * inverse_alpha;
                green += green >> 8;
                rgba[offset + 1] = static_cast<std::uint8_t>(green >> 8);

                std::uint32_t blue = blue_source_term +
                    static_cast<std::uint32_t>(rgba[offset + 2]) * inverse_alpha;
                blue += blue >> 8;
                rgba[offset + 2] = static_cast<std::uint8_t>(blue >> 8);
            }
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

        const __m256i zero = _mm256_setzero_si256();
        const __m256i rgb_mask = _mm256_set1_epi32(static_cast<int>(packed_rgb_mask));
        __m256i vector_sum = _mm256_setzero_si256();
        std::size_t offset = 0;
        for (; offset + 32 <= left.size(); offset += 32)
        {
            const __m256i left_pixels = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(left.data() + offset));
            const __m256i right_pixels = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(right.data() + offset));
            const __m256i difference = _mm256_or_si256(
                _mm256_subs_epu8(left_pixels, right_pixels),
                _mm256_subs_epu8(right_pixels, left_pixels));
            vector_sum = _mm256_add_epi64(
                vector_sum,
                _mm256_sad_epu8(_mm256_and_si256(difference, rgb_mask), zero));
        }

        alignas(32) std::array<std::uint64_t, 4> partial_sums {};
        _mm256_store_si256(reinterpret_cast<__m256i*>(partial_sums.data()), vector_sum);
        std::uint64_t total = partial_sums[0] + partial_sums[1] + partial_sums[2] + partial_sums[3];
        for (; offset < left.size(); offset += rgba_channel_count)
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
