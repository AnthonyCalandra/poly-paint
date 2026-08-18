#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace poly_paint
{
    /** @brief Owns a target image and scores candidate RGBA images against it.
     *  @details Alpha bytes are retained but excluded from fitness.
     */
    class ImageSimilarityScorer
    {
    public:
        /** @brief Copies a target RGBA image with the specified dimensions. */
        ImageSimilarityScorer(
            std::size_t width,
            std::size_t height,
            std::span<const std::uint8_t> target_rgba);

        /** @brief Returns the target width in pixels. */
        [[nodiscard]] std::size_t width() const noexcept { return m_width; }
        /** @brief Returns the target height in pixels. */
        [[nodiscard]] std::size_t height() const noexcept { return m_height; }
        /** @brief Returns the owned target's row-major RGBA bytes. */
        [[nodiscard]] std::span<const std::uint8_t> target_rgba() const noexcept { return m_target_rgba; }

        /** @brief Scores a full-size candidate image against the target. */
        [[nodiscard]] double score(
            std::span<const std::uint8_t> candidate_rgba,
            std::size_t width,
            std::size_t height) const;

        /** @brief Computes RGB difference for consecutive target rows.
         *  @details @p candidate_rgba is a compact buffer containing only the requested rows.
         */
        [[nodiscard]] std::uint64_t difference_for_rows(
            std::span<const std::uint8_t> candidate_rgba,
            std::size_t first_row,
            std::size_t row_count) const;

        /** @brief Converts a total RGB difference into the normalized similarity score. */
        [[nodiscard]] double score_from_total_difference(std::uint64_t total_difference) const;

    private:
        std::size_t m_width {};
        std::size_t m_height {};
        std::vector<std::uint8_t> m_target_rgba;
    };
}
