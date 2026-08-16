#include "image_similarity_scorer.h"

#include "image_dimensions.h"
#include "pixel_kernels.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace poly_paint
{
    ImageSimilarityScorer::ImageSimilarityScorer(
        std::size_t width,
        std::size_t height,
        std::span<const std::uint8_t> target_rgba)
        : m_width(width)
        , m_height(height)
    {
        if (width == 0 || height == 0 ||
            target_rgba.size() != checked_rgba_byte_count(width, height))
        {
            throw std::invalid_argument("ImageSimilarityScorer requires a complete RGBA image.");
        }
        m_target_rgba.assign(target_rgba.begin(), target_rgba.end());
    }

    double ImageSimilarityScorer::score(
        std::span<const std::uint8_t> candidate_rgba,
        std::size_t width,
        std::size_t height) const
    {
        if (width != m_width || height != m_height || candidate_rgba.size() != m_target_rgba.size())
        {
            return 0.0;
        }
        return score_from_total_difference(difference_for_rows(candidate_rgba, 0, height));
    }

    std::uint64_t ImageSimilarityScorer::difference_for_rows(
        std::span<const std::uint8_t> candidate_rgba,
        std::size_t first_row,
        std::size_t row_count) const
    {
        if (row_count == 0 || row_count > m_height || first_row > m_height - row_count)
        {
            return 0;
        }

        const std::size_t byte_count = checked_rgba_byte_count(m_width, row_count);
        if (candidate_rgba.size() != byte_count)
        {
            return 0;
        }
        const std::size_t target_offset = checked_rgba_byte_count(m_width, first_row);
        const std::span<const std::uint8_t> target_rows {
            m_target_rgba.data() + target_offset,
            byte_count
        };
        return detail::rgb_absolute_difference(target_rows, candidate_rgba);
    }

    double ImageSimilarityScorer::score_from_total_difference(std::uint64_t total_difference) const
    {
        const double maximum_difference =
            static_cast<double>(m_target_rgba.size() / rgba_channel_count) *
            static_cast<double>(rgb_channel_count) *
            static_cast<double>(std::numeric_limits<std::uint8_t>::max());
        const double normalized_difference = static_cast<double>(total_difference) / maximum_difference;
        return std::clamp(1.0 - normalized_difference, 0.0, 1.0);
    }
}
