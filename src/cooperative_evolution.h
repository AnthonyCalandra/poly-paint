#pragma once

#include "evolution_runner.h"
#include "polygon_evolution.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>

namespace poly_paint::detail
{
    /** @brief Thread-safe pause and cancellation state for an evolution run. */
    class EvolutionRunControl
    {
    public:
        /** @brief Clears pause and cancellation requests before a new run. */
        void reset() noexcept;
        /** @brief Requests that the worker pause after its current generation. */
        void request_pause() noexcept;
        /** @brief Releases a paused worker. */
        void resume() noexcept;
        /** @brief Requests that the worker stop after its current generation. */
        void request_stop() noexcept;

        /** @brief Waits while paused and returns whether evolution should continue. */
        [[nodiscard]] bool continue_after_generation();
        /** @brief Returns whether a pause has been requested. */
        [[nodiscard]] bool pause_requested() const noexcept;
        /** @brief Returns whether cancellation has been requested. */
        [[nodiscard]] bool cancel_requested() const noexcept;

    private:
        std::atomic<bool> m_cancel_requested {false};
        std::atomic<bool> m_pause_requested {false};
        std::mutex m_pause_mutex;
        std::condition_variable m_pause_condition;
    };

    /** @brief Thread-safe latest-value mailbox for evolution snapshots. */
    class EvolutionUpdateMailbox
    {
    public:
        /** @brief Discards a pending update. */
        void reset();
        /** @brief Publishes an update, replacing any older unread update. */
        void publish(EvolutionUpdate update);
        /** @brief Takes the newest pending update, if any. */
        [[nodiscard]] std::optional<EvolutionUpdate> take_latest();

    private:
        std::mutex m_mutex;
        std::optional<EvolutionUpdate> m_latest;
    };

    /** @brief Runs cooperative polygon evolution until stopped or complete. */
    void run_cooperative_evolution(
        const ImageSimilarityScorer& target,
        const EvolutionRunSettings& settings,
        std::span<const ContrastSeed> seeds,
        EvolutionRunControl& control,
        EvolutionUpdateMailbox& updates);
}
