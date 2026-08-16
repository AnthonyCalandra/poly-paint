#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace poly_paint
{
    // Owns the opened target image and scores candidate RGBA images against it.
    // Alpha is retained but excluded from fitness.
    class ImageSimilarityScorer
    {
    public:
        ImageSimilarityScorer(
            std::size_t width,
            std::size_t height,
            std::span<const std::uint8_t> target_rgba);

        [[nodiscard]] std::size_t width() const noexcept { return m_width; }
        [[nodiscard]] std::size_t height() const noexcept { return m_height; }
        [[nodiscard]] std::span<const std::uint8_t> target_rgba() const noexcept { return m_target_rgba; }

        [[nodiscard]] double score(
            std::span<const std::uint8_t> candidate_rgba,
            std::size_t width,
            std::size_t height) const;

        // candidate_rgba is a compact buffer containing the requested rows.
        [[nodiscard]] std::uint64_t difference_for_rows(
            std::span<const std::uint8_t> candidate_rgba,
            std::size_t first_row,
            std::size_t row_count) const;

        [[nodiscard]] double score_from_total_difference(std::uint64_t total_difference) const;

    private:
        std::size_t m_width {};
        std::size_t m_height {};
        std::vector<std::uint8_t> m_target_rgba;
    };
}
