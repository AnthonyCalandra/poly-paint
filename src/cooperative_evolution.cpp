#include "cooperative_evolution.h"

#include "evolution_strategy.h"
#include "image_dimensions.h"
#include "polygon_rasterizer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <thread>
#include <utility>
#include <vector>

namespace poly_paint::detail
{
    void EvolutionRunControl::reset() noexcept
    {
        m_cancel_requested = false;
        m_pause_requested = false;
    }

    void EvolutionRunControl::request_pause() noexcept
    {
        m_pause_requested = true;
    }

    void EvolutionRunControl::resume() noexcept
    {
        m_pause_requested = false;
        m_pause_condition.notify_all();
    }

    void EvolutionRunControl::request_stop() noexcept
    {
        m_cancel_requested = true;
        resume();
    }

    bool EvolutionRunControl::continue_after_generation()
    {
        if (m_pause_requested)
        {
            std::unique_lock lock(m_pause_mutex);
            m_pause_condition.wait(lock, [this]
            {
                return !m_pause_requested.load() || m_cancel_requested.load();
            });
        }
        return !m_cancel_requested.load();
    }

    bool EvolutionRunControl::pause_requested() const noexcept
    {
        return m_pause_requested;
    }

    bool EvolutionRunControl::cancel_requested() const noexcept
    {
        return m_cancel_requested;
    }

    void EvolutionUpdateMailbox::reset()
    {
        std::lock_guard lock(m_mutex);
        m_latest.reset();
    }

    void EvolutionUpdateMailbox::publish(EvolutionUpdate update)
    {
        std::lock_guard lock(m_mutex);
        m_latest = std::move(update);
    }

    std::optional<EvolutionUpdate> EvolutionUpdateMailbox::take_latest()
    {
        std::lock_guard lock(m_mutex);
        std::optional<EvolutionUpdate> update = std::move(m_latest);
        m_latest.reset();
        return update;
    }

    namespace
    {
        using PolygonStrategy = EvolutionStrategy<PolygonCollection>;

        constexpr std::size_t migration_interval = 32;
        constexpr std::size_t tile_row_count = 64;
        constexpr auto update_interval = std::chrono::milliseconds(50);

        [[nodiscard]] std::size_t resolve_island_count(const EvolutionRunSettings& settings)
        {
            if (settings.island_count > 0)
            {
                return settings.island_count;
            }
            return std::max<std::size_t>(1, std::thread::hardware_concurrency());
        }

        [[nodiscard]] std::vector<std::uint32_t> make_island_seeds(std::size_t count)
        {
            std::mt19937 seed_engine(std::random_device {}());
            std::vector<std::uint32_t> seeds(count);
            std::ranges::generate(seeds, [&seed_engine]
            {
                return seed_engine();
            });
            return seeds;
        }

        class SharedChampion
        {
        public:
            explicit SharedChampion(EvolutionUpdateMailbox& updates)
                : m_updates(updates)
            {
            }

            void consider(
                const PolygonCollection& candidate,
                std::span<const std::uint8_t> render,
                double score,
                std::size_t island)
            {
                std::lock_guard lock(m_mutex);
                if (score <= m_best_score)
                {
                    return;
                }

                m_best_individual = candidate;
                m_best_render.assign(render.begin(), render.end());
                m_best_score = score;
                m_owner_island = island;
                ++m_version;
            }

            [[nodiscard]] std::optional<PolygonStrategy::Migrant> migrant_for(
                std::size_t island,
                std::size_t generation,
                std::uint64_t& imported_version)
            {
                if (generation % migration_interval != 0)
                {
                    return std::nullopt;
                }

                std::lock_guard lock(m_mutex);
                if (!m_best_individual ||
                    m_owner_island == island ||
                    m_version <= imported_version)
                {
                    return std::nullopt;
                }

                imported_version = m_version;
                return PolygonStrategy::Migrant {*m_best_individual, m_best_score};
            }

            void publish(std::size_t generation, bool force = false)
            {
                const auto now = std::chrono::steady_clock::now();
                std::lock_guard lock(m_mutex);
                m_maximum_generation = std::max(m_maximum_generation, generation);
                const bool best_changed = m_version != m_published_version;
                if (!m_best_individual ||
                    (!force && !best_changed && now - m_last_update < update_interval))
                {
                    return;
                }

                m_published_version = m_version;
                m_last_update = now;
                // Publish while holding m_mutex so an older snapshot cannot arrive
                // after a newer global-best version from another island.
                m_updates.publish(
                    EvolutionUpdate {m_best_render, m_maximum_generation, m_best_score});
            }

        private:
            EvolutionUpdateMailbox& m_updates;
            std::mutex m_mutex;
            std::optional<PolygonCollection> m_best_individual;
            std::vector<std::uint8_t> m_best_render;
            double m_best_score {-std::numeric_limits<double>::infinity()};
            std::size_t m_owner_island {};
            std::uint64_t m_version {};
            std::uint64_t m_published_version {};
            std::size_t m_maximum_generation {};
            std::chrono::steady_clock::time_point m_last_update {};
        };

        [[nodiscard]] std::uint64_t render_and_measure_difference(
            const PolygonCollection& candidate,
            const ImageSimilarityScorer& target,
            std::span<std::uint8_t> render)
        {
            std::uint64_t total_difference = 0;
            for (std::size_t first_row = 0;
                 first_row < target.height();
                 first_row += tile_row_count)
            {
                const std::size_t row_count =
                    std::min(tile_row_count, target.height() - first_row);
                const std::size_t tile_size =
                    checked_rgba_byte_count(target.width(), row_count);
                const std::size_t tile_offset =
                    checked_rgba_byte_count(target.width(), first_row);
                const std::span<std::uint8_t> tile = render.subspan(tile_offset, tile_size);
                rasterize_rows_into(
                    candidate,
                    target.width(),
                    target.height(),
                    first_row,
                    row_count,
                    tile);
                total_difference += target.difference_for_rows(tile, first_row, row_count);
            }
            return total_difference;
        }

        class CandidateEvaluator
        {
        public:
            CandidateEvaluator(
                const ImageSimilarityScorer& target,
                SharedChampion& champion,
                std::size_t island)
                : m_target(target)
                , m_champion(champion)
                , m_island(island)
                , m_render(checked_rgba_byte_count(target.width(), target.height()))
            {
            }

            [[nodiscard]] double operator()(const PolygonCollection& candidate)
            {
                const std::uint64_t difference =
                    render_and_measure_difference(candidate, m_target, m_render);
                const double score = m_target.score_from_total_difference(difference);
                m_champion.consider(candidate, m_render, score, m_island);
                return score;
            }

        private:
            const ImageSimilarityScorer& m_target;
            SharedChampion& m_champion;
            std::size_t m_island;
            std::vector<std::uint8_t> m_render;
        };

        class PolygonFactory
        {
        public:
            PolygonFactory(
                const ImageSimilarityScorer& target,
                const EvolutionRunSettings& settings,
                std::span<const ContrastSeed> seeds)
                : m_target(target)
                , m_settings(settings)
                , m_seeds(seeds)
            {
            }

            [[nodiscard]] PolygonCollection operator()(std::mt19937& random_engine) const
            {
                if (m_settings.initial_population == InitialPopulationMode::best_guess)
                {
                    return make_best_guess_polygon_collection(
                        m_seeds,
                        m_target.width(),
                        m_target.height(),
                        m_settings.polygon_count,
                        random_engine,
                        m_target.target_rgba());
                }
                return make_random_polygon_collection(
                    random_engine, m_settings.polygon_count, m_target.target_rgba());
            }

        private:
            const ImageSimilarityScorer& m_target;
            const EvolutionRunSettings& m_settings;
            std::span<const ContrastSeed> m_seeds;
        };

        class PolygonMutation
        {
        public:
            explicit PolygonMutation(std::span<const std::uint8_t> target_rgba)
                : m_target_rgba(target_rgba)
            {
            }

            [[nodiscard]] PolygonCollection operator()(
                const PolygonCollection& parent,
                std::mt19937& random_engine) const
            {
                return mutate_polygon_collection(parent, random_engine, m_target_rgba);
            }

        private:
            std::span<const std::uint8_t> m_target_rgba;
        };

        class GenerationObserver
        {
        public:
            GenerationObserver(
                SharedChampion& champion,
                EvolutionRunControl& control,
                std::size_t generation_limit,
                std::atomic<bool>& generation_limit_reached)
                : m_champion(champion)
                , m_control(control)
                , m_generation_limit(generation_limit)
                , m_generation_limit_reached(generation_limit_reached)
            {
            }

            [[nodiscard]] bool operator()(
                std::size_t generation,
                const PolygonCollection&,
                const double&)
            {
                const bool reached_limit = m_generation_limit > 0 &&
                    generation >= m_generation_limit &&
                    !m_generation_limit_reached.exchange(true);
                m_champion.publish(generation, reached_limit);
                if (reached_limit)
                {
                    m_control.request_pause();
                }
                return m_control.continue_after_generation();
            }

        private:
            SharedChampion& m_champion;
            EvolutionRunControl& m_control;
            std::size_t m_generation_limit;
            std::atomic<bool>& m_generation_limit_reached;
        };

        class MigrantSource
        {
        public:
            MigrantSource(SharedChampion& champion, std::size_t island)
                : m_champion(champion)
                , m_island(island)
            {
            }

            [[nodiscard]] std::optional<PolygonStrategy::Migrant> operator()(
                std::size_t generation)
            {
                return m_champion.migrant_for(m_island, generation, m_imported_version);
            }

        private:
            SharedChampion& m_champion;
            std::size_t m_island;
            std::uint64_t m_imported_version {};
        };

        [[nodiscard]] PolygonStrategy::Settings make_strategy_settings(
            const EvolutionRunSettings& settings)
        {
            PolygonStrategy::Settings strategy_settings;
            strategy_settings.parent_count = settings.parent_count;
            strategy_settings.offspring_count = settings.offspring_count;
            // The configured limit pauses the cooperative run through its
            // observer, so the strategy itself must remain resumable.
            strategy_settings.max_generations = 0;
            // Each island performs its own evaluations. An internal pool would
            // multiply the worker count by the number of islands.
            strategy_settings.worker_count = 1;
            return strategy_settings;
        }

        class IslandEvolution
        {
        public:
            IslandEvolution(
                const ImageSimilarityScorer& target,
                const EvolutionRunSettings& settings,
                std::span<const ContrastSeed> seeds,
                SharedChampion& champion,
                EvolutionRunControl& control,
                std::atomic<bool>& generation_limit_reached,
                std::size_t island,
                std::uint32_t random_seed)
                : m_target(target)
                , m_settings(settings)
                , m_seeds(seeds)
                , m_champion(champion)
                , m_control(control)
                , m_generation_limit_reached(generation_limit_reached)
                , m_island(island)
                , m_random_seed(random_seed)
            {
            }

            void operator()()
            {
                PolygonFactory factory(m_target, m_settings, m_seeds);
                PolygonMutation mutation(m_target.target_rgba());
                CandidateEvaluator evaluator(m_target, m_champion, m_island);
                GenerationObserver observer(
                    m_champion,
                    m_control,
                    m_settings.maximum_generations,
                    m_generation_limit_reached);
                MigrantSource migrants(m_champion, m_island);
                PolygonStrategy strategy(
                    make_strategy_settings(m_settings),
                    std::ref(factory),
                    std::ref(mutation),
                    std::ref(evaluator),
                    {},
                    std::ref(observer),
                    std::ref(migrants));

                std::mt19937 random_engine(m_random_seed);
                [[maybe_unused]] PolygonStrategy::Result result = strategy.run(random_engine);
            }

        private:
            const ImageSimilarityScorer& m_target;
            const EvolutionRunSettings& m_settings;
            std::span<const ContrastSeed> m_seeds;
            SharedChampion& m_champion;
            EvolutionRunControl& m_control;
            std::atomic<bool>& m_generation_limit_reached;
            std::size_t m_island;
            std::uint32_t m_random_seed;
        };
    }

    void run_cooperative_evolution(
        const ImageSimilarityScorer& target,
        const EvolutionRunSettings& settings,
        std::span<const ContrastSeed> seeds,
        EvolutionRunControl& control,
        EvolutionUpdateMailbox& updates)
    {
        SharedChampion champion(updates);
        std::atomic<bool> generation_limit_reached {false};
        const std::size_t island_count = resolve_island_count(settings);
        const std::vector<std::uint32_t> random_seeds = make_island_seeds(island_count);

        std::vector<std::thread> islands;
        islands.reserve(island_count);
        for (std::size_t island = 0; island < island_count; ++island)
        {
            islands.emplace_back(IslandEvolution {
                target,
                settings,
                seeds,
                champion,
                control,
                generation_limit_reached,
                island,
                random_seeds[island]});
        }
        for (std::thread& island : islands)
        {
            island.join();
        }
        champion.publish(0, true);
    }
}
