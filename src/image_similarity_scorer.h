#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <stdexcept>
#include <vector>

namespace poly_paint
{
    // Owns the opened target image and scores a candidate RGBA image against it.
    // Only RGB is compared: alpha is retained in the buffers but does not affect
    // fitness. A pixel-perfect candidate scores 1.0; a candidate with the maximum
    // possible difference in every color channel scores 0.0.
    class ImageSimilarityScorer
    {
    public:
        ImageSimilarityScorer(int width, int height, const std::uint8_t* target_rgba)
            : m_width(width)
            , m_height(height)
        {
            if (width <= 0 || height <= 0 || target_rgba == nullptr)
            {
                throw std::invalid_argument("ImageSimilarityScorer requires a non-empty RGBA image.");
            }

            const std::size_t byte_count = rgba_byte_count(width, height);
            m_target_rgba.assign(target_rgba, target_rgba + byte_count);
        }

        [[nodiscard]] int width() const noexcept { return m_width; }
        [[nodiscard]] int height() const noexcept { return m_height; }
        [[nodiscard]] const std::uint8_t* target_rgba() const noexcept { return m_target_rgba.data(); }

        [[nodiscard]] double score(const std::uint8_t* candidate_rgba, int width, int height) const
        {
            if (candidate_rgba == nullptr || width != m_width || height != m_height)
            {
                return 0.0;
            }

            return score_from_total_difference(difference_for_rows(candidate_rgba, 0, height));
        }

        // Returns the RGB absolute difference for a compact candidate buffer
        // containing [first_row, first_row + row_count) of the image.
        [[nodiscard]] std::uint64_t difference_for_rows(
            const std::uint8_t* candidate_rgba,
            int first_row,
            int row_count) const
        {
            if (candidate_rgba == nullptr || first_row < 0 || row_count <= 0 ||
                first_row > m_height - row_count)
            {
                return 0;
            }

            // AVX2 compares eight RGBA pixels (32 bytes) at a time. The alpha
            // byte of each pixel is masked out before horizontally summing the
            // unsigned RGB differences.
            const __m256i zero = _mm256_setzero_si256();
            const __m256i rgb_mask = _mm256_set1_epi32(0x00FFFFFF);
            __m256i vector_sum = _mm256_setzero_si256();
            const std::size_t byte_count = rgba_byte_count(m_width, row_count);
            const std::uint8_t* target_rgba = m_target_rgba.data() +
                rgba_byte_count(m_width, first_row);
            std::size_t index = 0;
            for (; index + 32 <= byte_count; index += 32)
            {
                const __m256i target = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(target_rgba + index));
                const __m256i candidate = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(candidate_rgba + index));
                const __m256i difference = _mm256_or_si256(
                    _mm256_subs_epu8(target, candidate),
                    _mm256_subs_epu8(candidate, target));
                vector_sum = _mm256_add_epi64(
                    vector_sum,
                    _mm256_sad_epu8(_mm256_and_si256(difference, rgb_mask), zero));
            }

            alignas(32) std::uint64_t partial_sums[4] {};
            _mm256_store_si256(reinterpret_cast<__m256i*>(partial_sums), vector_sum);
            std::uint64_t total_difference =
                partial_sums[0] + partial_sums[1] + partial_sums[2] + partial_sums[3];

            for (; index < byte_count; index += 4)
            {
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const int difference = static_cast<int>(target_rgba[index + channel]) -
                        static_cast<int>(candidate_rgba[index + channel]);
                    total_difference += static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
                }
            }

            return total_difference;
        }

        [[nodiscard]] double score_from_total_difference(std::uint64_t total_difference) const
        {
            const double maximum_difference =
                static_cast<double>(m_target_rgba.size() / 4 * 3) * static_cast<double>(UINT8_MAX);
            const double normalized_difference = static_cast<double>(total_difference) / maximum_difference;
            return std::clamp(1.0 - normalized_difference, 0.0, 1.0);
        }

        [[nodiscard]] double score(const std::vector<std::uint8_t>& candidate_rgba, int width, int height) const
        {
            if (candidate_rgba.size() != m_target_rgba.size())
            {
                return 0.0;
            }
            return score(candidate_rgba.data(), width, height);
        }

    private:
        static std::size_t rgba_byte_count(int width, int height)
        {
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        }

        int m_width {};
        int m_height {};
        std::vector<std::uint8_t> m_target_rgba;
    };
}
