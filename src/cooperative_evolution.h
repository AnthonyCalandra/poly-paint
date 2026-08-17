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
    class EvolutionRunControl
    {
    public:
        void reset() noexcept;
        void request_pause() noexcept;
        void resume() noexcept;
        void request_stop() noexcept;

        [[nodiscard]] bool continue_after_generation();
        [[nodiscard]] bool pause_requested() const noexcept;
        [[nodiscard]] bool cancel_requested() const noexcept;

    private:
        std::atomic<bool> m_cancel_requested {false};
        std::atomic<bool> m_pause_requested {false};
        std::mutex m_pause_mutex;
        std::condition_variable m_pause_condition;
    };

    class EvolutionUpdateMailbox
    {
    public:
        void reset();
        void publish(EvolutionUpdate update);
        [[nodiscard]] std::optional<EvolutionUpdate> take_latest();

    private:
        std::mutex m_mutex;
        std::optional<EvolutionUpdate> m_latest;
    };

    void run_cooperative_evolution(
        const ImageSimilarityScorer& target,
        const EvolutionRunSettings& settings,
        std::span<const ContrastSeed> seeds,
        EvolutionRunControl& control,
        EvolutionUpdateMailbox& updates);
}
