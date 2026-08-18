#pragma once

#include "image_similarity_scorer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace poly_paint
{
    /** @brief Selects how an evolution run creates its first population. */
    enum class InitialPopulationMode
    {
        /** @brief Start from fully randomized polygons. */
        randomized,
        /** @brief Seed polygons from high-contrast image regions. */
        best_guess
    };

    /** @brief Configures a background cooperative evolution run. */
    struct EvolutionRunSettings
    {
        /** @brief Lowest accepted parent or offspring population size. */
        static constexpr std::size_t minimum_population_count = 1;
        /** @brief Highest accepted parent or offspring population size. */
        static constexpr std::size_t maximum_population_count = 50;

        /** @brief Generation limit; zero means no limit. */
        std::size_t maximum_generations {};
        /** @brief Number of polygons in each candidate image. */
        std::size_t polygon_count {};
        /** @brief Number of selected parents per island. */
        std::size_t parent_count {5};
        /** @brief Number of mutated children per island generation. */
        std::size_t offspring_count {1};
        /** @brief Number of cooperative islands; zero uses available logical processors. */
        std::size_t island_count {};
        /** @brief Algorithm used to create the initial polygon population. */
        InitialPopulationMode initial_population {InitialPopulationMode::randomized};
    };

    /** @brief Immutable best-result snapshot published by a running evolution. */
    struct EvolutionUpdate
    {
        /** @brief Row-major RGBA bytes of the best candidate. */
        std::vector<std::uint8_t> rgba;
        /** @brief Generation that produced this candidate. */
        std::size_t generation {};
        /** @brief Similarity score of this candidate. */
        double score {};
    };

    /** @brief Owns a background evolution job and exposes its control and updates. */
    class EvolutionRunner
    {
    public:
        /** @brief Creates an idle evolution runner. */
        EvolutionRunner();
        /** @brief Stops and joins a running job before destroying the runner. */
        ~EvolutionRunner();

        /** @brief Disables copying because the runner owns synchronization state. */
        EvolutionRunner(const EvolutionRunner&) = delete;
        /** @brief Disables copy assignment because the runner owns synchronization state. */
        EvolutionRunner& operator=(const EvolutionRunner&) = delete;

        /** @brief Starts a new run that approximates @p target using @p settings. */
        void start(
            const ImageSimilarityScorer& target,
            const EvolutionRunSettings& settings);
        /** @brief Requests a pause after the current generation completes. */
        void request_pause() noexcept;
        /** @brief Resumes a paused run. */
        void resume() noexcept;
        /** @brief Requests a stop after the current generation completes. */
        void request_stop() noexcept;
        /** @brief Requests stop and blocks until the worker thread has exited. */
        void stop_and_wait() noexcept;

        /** @brief Returns whether a worker job is active. */
        [[nodiscard]] bool running() const noexcept;
        /** @brief Returns whether the active job has a pending pause request. */
        [[nodiscard]] bool pause_requested() const noexcept;
        /** @brief Takes the newest available generation snapshot. */
        [[nodiscard]] std::optional<EvolutionUpdate> take_latest_update();
        /** @brief Joins a finished job and returns whether the user stopped it. */
        [[nodiscard]] std::optional<bool> join_if_finished();

    private:
        /** @brief Hides worker-thread implementation details from the public header. */
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
