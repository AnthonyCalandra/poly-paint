#pragma once

#include "image_similarity_scorer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace poly_paint
{
    enum class InitialPopulationMode
    {
        randomized,
        best_guess
    };

    struct EvolutionRunSettings
    {
        static constexpr std::size_t minimum_population_count = 1;
        static constexpr std::size_t maximum_population_count = 50;

        std::size_t maximum_generations {};
        std::size_t polygon_count {};
        std::size_t parent_count {5};
        std::size_t offspring_count {1};
        InitialPopulationMode initial_population {InitialPopulationMode::randomized};
    };

    struct EvolutionUpdate
    {
        std::vector<std::uint8_t> rgba;
        std::size_t generation {};
        double score {};
    };

    // Owns the background evolution job and its synchronization. The UI only
    // submits commands and consumes immutable generation snapshots.
    class EvolutionRunner
    {
    public:
        EvolutionRunner();
        ~EvolutionRunner();

        EvolutionRunner(const EvolutionRunner&) = delete;
        EvolutionRunner& operator=(const EvolutionRunner&) = delete;

        void start(
            const ImageSimilarityScorer& target,
            const EvolutionRunSettings& settings);
        void request_pause() noexcept;
        void resume() noexcept;
        void request_stop() noexcept;
        void stop_and_wait() noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] bool pause_requested() const noexcept;
        [[nodiscard]] std::optional<EvolutionUpdate> take_latest_update();
        // Returns whether a completed job was stopped by the user.
        [[nodiscard]] std::optional<bool> join_if_finished();

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
