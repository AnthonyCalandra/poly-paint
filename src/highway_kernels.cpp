#include "pixel_kernels.h"

#include "image_dimensions.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "highway_kernels.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace poly_paint::detail::HWY_NAMESPACE
{
    namespace hn = hwy::HWY_NAMESPACE;

    void FillRgba(
        std::uint8_t* rgba,
        std::size_t size,
        std::uint8_t red,
        std::uint8_t green,
        std::uint8_t blue,
        std::uint8_t alpha)
    {
        const hn::ScalableTag<std::uint8_t> d8;
        const std::size_t lanes = hn::Lanes(d8);
        const std::size_t pixel_count = size / rgba_channel_count;
        const auto reds = hn::Set(d8, red);
        const auto greens = hn::Set(d8, green);
        const auto blues = hn::Set(d8, blue);
        const auto alphas = hn::Set(d8, alpha);

        std::size_t pixel = 0;
        for (; pixel + lanes <= pixel_count; pixel += lanes)
        {
            hn::StoreInterleaved4(
                reds,
                greens,
                blues,
                alphas,
                d8,
                rgba + pixel * rgba_channel_count);
        }
        for (; pixel < pixel_count; ++pixel)
        {
            const std::size_t offset = pixel * rgba_channel_count;
            rgba[offset] = red;
            rgba[offset + 1] = green;
            rgba[offset + 2] = blue;
            rgba[offset + alpha_channel_index] = alpha;
        }
    }

    void BlendSourceOverOpaque(
        std::uint8_t* rgba,
        std::size_t size,
        std::uint8_t source_red,
        std::uint8_t source_green,
        std::uint8_t source_blue,
        std::uint8_t source_alpha)
    {
        constexpr std::uint16_t maximum_channel =
            std::numeric_limits<std::uint8_t>::max();
        const hn::ScalableTag<std::uint16_t> d16;
        const hn::Rebind<std::uint8_t, decltype(d16)> d8;
        const std::size_t lanes = hn::Lanes(d16);
        const std::size_t pixel_count = size / rgba_channel_count;
        const auto inverse_alpha = hn::Set(
            d16, static_cast<std::uint16_t>(maximum_channel - source_alpha));
        const auto red_source_term = hn::Set(
            d16,
            static_cast<std::uint16_t>(source_red * source_alpha + 128));
        const auto green_source_term = hn::Set(
            d16,
            static_cast<std::uint16_t>(source_green * source_alpha + 128));
        const auto blue_source_term = hn::Set(
            d16,
            static_cast<std::uint16_t>(source_blue * source_alpha + 128));
        const auto opaque = hn::Set(d8, static_cast<std::uint8_t>(maximum_channel));

        const auto blend_channel = [&](const auto destination, const auto source_term)
        {
            auto blended = hn::Add(
                source_term,
                hn::Mul(hn::PromoteTo(d16, destination), inverse_alpha));
            blended = hn::Add(blended, hn::ShiftRight<8>(blended));
            return hn::DemoteTo(d8, hn::ShiftRight<8>(blended));
        };

        std::size_t pixel = 0;
        for (; pixel + lanes <= pixel_count; pixel += lanes)
        {
            hn::Vec<decltype(d8)> reds;
            hn::Vec<decltype(d8)> greens;
            hn::Vec<decltype(d8)> blues;
            hn::Vec<decltype(d8)> alphas;
            hn::LoadInterleaved4(
                d8,
                rgba + pixel * rgba_channel_count,
                reds,
                greens,
                blues,
                alphas);
            hn::StoreInterleaved4(
                blend_channel(reds, red_source_term),
                blend_channel(greens, green_source_term),
                blend_channel(blues, blue_source_term),
                opaque,
                d8,
                rgba + pixel * rgba_channel_count);
        }

        const std::uint32_t alpha = source_alpha;
        const std::uint32_t inverse_source_alpha = maximum_channel - alpha;
        const std::uint32_t scalar_red_source_term = source_red * alpha + 128;
        const std::uint32_t scalar_green_source_term = source_green * alpha + 128;
        const std::uint32_t scalar_blue_source_term = source_blue * alpha + 128;
        for (; pixel < pixel_count; ++pixel)
        {
            const std::size_t offset = pixel * rgba_channel_count;
            std::uint32_t red =
                scalar_red_source_term + rgba[offset] * inverse_source_alpha;
            red += red >> 8;
            rgba[offset] = static_cast<std::uint8_t>(red >> 8);

            std::uint32_t green =
                scalar_green_source_term + rgba[offset + 1] * inverse_source_alpha;
            green += green >> 8;
            rgba[offset + 1] = static_cast<std::uint8_t>(green >> 8);

            std::uint32_t blue =
                scalar_blue_source_term + rgba[offset + 2] * inverse_source_alpha;
            blue += blue >> 8;
            rgba[offset + 2] = static_cast<std::uint8_t>(blue >> 8);
            rgba[offset + alpha_channel_index] =
                static_cast<std::uint8_t>(maximum_channel);
        }
    }

    std::uint64_t RgbAbsoluteDifference(
        const std::uint8_t* left,
        const std::uint8_t* right,
        std::size_t size)
    {
        constexpr std::size_t vectors_per_reduction = 4'096;
        const hn::ScalableTag<std::uint32_t> d32;
        const hn::Rebind<std::uint8_t, decltype(d32)> d8;
        const std::size_t lanes = hn::Lanes(d32);
        const std::size_t pixel_count = size / rgba_channel_count;
        auto vector_sum = hn::Zero(d32);
        std::size_t vectors_accumulated = 0;
        std::uint64_t total = 0;

        std::size_t pixel = 0;
        for (; pixel + lanes <= pixel_count; pixel += lanes)
        {
            hn::Vec<decltype(d8)> left_reds;
            hn::Vec<decltype(d8)> left_greens;
            hn::Vec<decltype(d8)> left_blues;
            hn::Vec<decltype(d8)> left_alphas;
            hn::Vec<decltype(d8)> right_reds;
            hn::Vec<decltype(d8)> right_greens;
            hn::Vec<decltype(d8)> right_blues;
            hn::Vec<decltype(d8)> right_alphas;
            hn::LoadInterleaved4(
                d8,
                left + pixel * rgba_channel_count,
                left_reds,
                left_greens,
                left_blues,
                left_alphas);
            hn::LoadInterleaved4(
                d8,
                right + pixel * rgba_channel_count,
                right_reds,
                right_greens,
                right_blues,
                right_alphas);

            const auto red_difference =
                hn::PromoteTo(d32, hn::AbsDiff(left_reds, right_reds));
            const auto green_difference =
                hn::PromoteTo(d32, hn::AbsDiff(left_greens, right_greens));
            const auto blue_difference =
                hn::PromoteTo(d32, hn::AbsDiff(left_blues, right_blues));
            vector_sum = hn::Add(
                vector_sum,
                hn::Add(red_difference, hn::Add(green_difference, blue_difference)));

            if (++vectors_accumulated == vectors_per_reduction)
            {
                total += hn::ReduceSum(d32, vector_sum);
                vector_sum = hn::Zero(d32);
                vectors_accumulated = 0;
            }
        }
        total += hn::ReduceSum(d32, vector_sum);

        for (; pixel < pixel_count; ++pixel)
        {
            const std::size_t offset = pixel * rgba_channel_count;
            for (std::size_t channel = 0; channel < rgb_channel_count; ++channel)
            {
                const int difference = left[offset + channel] - right[offset + channel];
                total += difference < 0 ? -difference : difference;
            }
        }
        return total;
    }
}

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace poly_paint::detail
{
    namespace
    {
        void validate_rgba(std::span<const std::uint8_t> rgba)
        {
            if (rgba.size() % rgba_channel_count != 0)
            {
                throw std::invalid_argument("RGBA buffers must contain complete pixels.");
            }
        }
    }

    HWY_EXPORT(FillRgba);
    HWY_EXPORT(BlendSourceOverOpaque);
    HWY_EXPORT(RgbAbsoluteDifference);

    void fill_rgba_pixels(std::span<std::uint8_t> rgba, RgbaColor color)
    {
        validate_rgba(rgba);
        HWY_DYNAMIC_DISPATCH(FillRgba)(
            rgba.data(), rgba.size(), color.r, color.g, color.b, color.a);
    }

    void blend_source_over_opaque(
        std::span<const MutableRgbaSpan> rgba_spans,
        RgbaColor source)
    {
        for (const MutableRgbaSpan rgba : rgba_spans)
        {
            validate_rgba(rgba);
        }

        const auto blend = HWY_DYNAMIC_POINTER(BlendSourceOverOpaque);
        for (const MutableRgbaSpan rgba : rgba_spans)
        {
            blend(
                rgba.data(),
                rgba.size(),
                source.r,
                source.g,
                source.b,
                source.a);
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
        return HWY_DYNAMIC_DISPATCH(RgbAbsoluteDifference)(
            left.data(), right.data(), left.size());
    }
}

#endif
