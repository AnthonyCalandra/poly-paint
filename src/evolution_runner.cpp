#include "evolution_runner.h"

#include "evolution_strategy.h"
#include "image_dimensions.h"
#include "polygon_evolution.h"
#include "polygon_rasterizer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace poly_paint
{
    class EvolutionRunner::Impl
    {
    public:
        ~Impl()
        {
            stop_and_wait();
        }

        void start(
            const ImageSimilarityScorer& target,
            const EvolutionRunSettings& settings)
        {
            if (running)
            {
                throw std::logic_error("Cannot start an evolution job while another job is running.");
            }
            if (worker.joinable())
            {
                worker.join();
            }

            if (settings.parent_count < EvolutionRunSettings::minimum_population_count ||
                settings.parent_count > EvolutionRunSettings::maximum_population_count ||
                settings.offspring_count < EvolutionRunSettings::minimum_population_count ||
                settings.offspring_count > EvolutionRunSettings::maximum_population_count)
            {
                throw std::invalid_argument(
                    "Mu and lambda must each be between one and fifty.");
            }

            std::vector<ContrastSeed> seeds;
            if (settings.initial_population == InitialPopulationMode::best_guess)
            {
                seeds = find_high_contrast_seeds(
                    target.target_rgba(), target.width(), target.height(), settings.polygon_count);
            }

            {
                std::lock_guard lock(update_mutex);
                latest_update.reset();
            }
            cancel_requested = false;
            pause_requested = false;
            running = true;
            worker = std::thread([
                this,
                target,
                settings,
                seeds = std::move(seeds)]() mutable
            {
                run(
                    target,
                    settings,
                    std::move(seeds));
                running = false;
            });
        }

        void request_pause() noexcept
        {
            pause_requested = true;
        }

        void resume() noexcept
        {
            pause_requested = false;
            pause_condition.notify_all();
        }

        void request_stop() noexcept
        {
            cancel_requested = true;
            resume();
        }

        void stop_and_wait() noexcept
        {
            request_stop();
            if (worker.joinable())
            {
                worker.join();
            }
        }

        [[nodiscard]] std::optional<EvolutionUpdate> take_latest_update()
        {
            std::lock_guard lock(update_mutex);
            std::optional<EvolutionUpdate> update = std::move(latest_update);
            latest_update.reset();
            return update;
        }

        [[nodiscard]] std::optional<bool> join_if_finished()
        {
            if (running || !worker.joinable())
            {
                return std::nullopt;
            }
            const bool stopped = cancel_requested;
            worker.join();
            return stopped;
        }

        std::thread worker;
        std::atomic<bool> running {false};
        std::atomic<bool> cancel_requested {false};
        std::atomic<bool> pause_requested {false};
        std::mutex pause_mutex;
        std::condition_variable pause_condition;
        std::mutex update_mutex;
        std::optional<EvolutionUpdate> latest_update;

    private:
        void run(
            const ImageSimilarityScorer& target,
            const EvolutionRunSettings& run_settings,
            std::vector<ContrastSeed> seeds)
        {
            using Strategy = EvolutionStrategy<PolygonCollection>;
            const std::size_t image_byte_count =
                checked_rgba_byte_count(target.width(), target.height());
            const std::size_t island_count = run_settings.island_count > 0
                ? run_settings.island_count
                : std::max<std::size_t>(1, std::thread::hardware_concurrency());
            constexpr std::size_t migration_interval = 32;
            constexpr auto update_interval = std::chrono::milliseconds(50);

            struct CooperativeState
            {
                std::mutex mutex;
                std::optional<PolygonCollection> best_individual;
                std::vector<std::uint8_t> best_render;
                double best_score {-std::numeric_limits<double>::infinity()};
                std::size_t owner_island {};
                std::uint64_t version {};
                std::uint64_t published_version {};
                std::size_t maximum_generation {};
                std::chrono::steady_clock::time_point last_update {};
            } shared;
            shared.best_render.resize(image_byte_count);

            const auto publish_update = [this, &shared](std::size_t generation, bool force)
            {
                const auto now = std::chrono::steady_clock::now();
                std::lock_guard shared_lock(shared.mutex);
                shared.maximum_generation = std::max(shared.maximum_generation, generation);
                const bool best_changed = shared.version != shared.published_version;
                if (shared.best_individual &&
                    (force || best_changed || now - shared.last_update >= update_interval))
                {
                    EvolutionUpdate update {
                        shared.best_render,
                        shared.maximum_generation,
                        shared.best_score};
                    shared.published_version = shared.version;
                    shared.last_update = now;
                    // Keep publication ordered by the shared-best version. Releasing
                    // shared.mutex first would let an older snapshot overwrite a newer one.
                    std::lock_guard update_lock(update_mutex);
                    latest_update = std::move(update);
                }
            };

            std::mt19937 seed_engine(std::random_device {}());
            std::vector<std::uint32_t> island_seeds(island_count);
            std::ranges::generate(island_seeds, [&seed_engine]
            {
                return seed_engine();
            });

            std::vector<std::thread> islands;
            islands.reserve(island_count);
            for (std::size_t island = 0; island < island_count; ++island)
            {
                islands.emplace_back([
                    this,
                    &target,
                    &run_settings,
                    &seeds,
                    &shared,
                    &publish_update,
                    image_byte_count,
                    island,
                    random_seed = island_seeds[island]]
                {
                    Strategy::Settings settings;
                    settings.parent_count = run_settings.parent_count;
                    settings.offspring_count = run_settings.offspring_count;
                    settings.max_generations = run_settings.maximum_generations;
                    // The island thread performs its own evaluation work. An internal
                    // pool here would multiply the worker count by the island count.
                    settings.worker_count = 1;

                    std::uint64_t imported_version = 0;
                    Strategy strategy(
                        settings,
                        [&run_settings, &seeds, &target](std::mt19937& random_engine)
                        {
                            if (run_settings.initial_population == InitialPopulationMode::best_guess)
                            {
                                return make_best_guess_polygon_collection(
                                    seeds,
                                    target.width(),
                                    target.height(),
                                    run_settings.polygon_count,
                                    random_engine);
                            }
                            return make_random_polygon_collection(
                                random_engine, run_settings.polygon_count);
                        },
                        mutate_polygon_collection,
                        [&target, &shared, image_byte_count, island](
                            const PolygonCollection& candidate)
                        {
                            constexpr std::size_t tile_row_count = 64;
                            // Each island reuses its own complete render so publishing a
                            // new global champion never requires a second rasterization.
                            thread_local std::vector<std::uint8_t> candidate_render;
                            candidate_render.resize(image_byte_count);

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
                                const std::span<std::uint8_t> tile =
                                    std::span<std::uint8_t> {candidate_render}.subspan(
                                        tile_offset, tile_size);
                                rasterize_rows_into(
                                    candidate,
                                    target.width(),
                                    target.height(),
                                    first_row,
                                    row_count,
                                    tile);
                                total_difference +=
                                    target.difference_for_rows(tile, first_row, row_count);
                            }
                            const double score =
                                target.score_from_total_difference(total_difference);
                            {
                                std::lock_guard lock(shared.mutex);
                                if (score > shared.best_score)
                                {
                                    shared.best_individual = candidate;
                                    shared.best_render = candidate_render;
                                    shared.best_score = score;
                                    shared.owner_island = island;
                                    ++shared.version;
                                }
                            }
                            return score;
                        },
                        {},
                        [this, &publish_update](
                            std::size_t generation,
                            const PolygonCollection&,
                            const double&)
                        {
                            publish_update(generation, false);
                            if (pause_requested)
                            {
                                std::unique_lock lock(pause_mutex);
                                pause_condition.wait(lock, [this]
                                {
                                    return !pause_requested.load() || cancel_requested.load();
                                });
                            }
                            return !cancel_requested.load();
                        },
                        [&shared, &imported_version, island](std::size_t generation)
                            -> std::optional<Strategy::Migrant>
                        {
                            if (generation % migration_interval != 0)
                            {
                                return std::nullopt;
                            }
                            std::lock_guard lock(shared.mutex);
                            if (!shared.best_individual ||
                                shared.owner_island == island ||
                                shared.version <= imported_version)
                            {
                                return std::nullopt;
                            }
                            imported_version = shared.version;
                            return Strategy::Migrant {
                                *shared.best_individual,
                                shared.best_score};
                        });

                    std::mt19937 random_engine(random_seed);
                    [[maybe_unused]] Strategy::Result result = strategy.run(random_engine);
                });
            }
            for (std::thread& island : islands)
            {
                island.join();
            }
            publish_update(0, true);
        }
    };

    EvolutionRunner::EvolutionRunner()
        : m_impl(std::make_unique<Impl>())
    {
    }

    EvolutionRunner::~EvolutionRunner() = default;

    void EvolutionRunner::start(
        const ImageSimilarityScorer& target,
        const EvolutionRunSettings& settings)
    {
        m_impl->start(target, settings);
    }

    void EvolutionRunner::request_pause() noexcept { m_impl->request_pause(); }
    void EvolutionRunner::resume() noexcept { m_impl->resume(); }
    void EvolutionRunner::request_stop() noexcept { m_impl->request_stop(); }
    void EvolutionRunner::stop_and_wait() noexcept { m_impl->stop_and_wait(); }
    bool EvolutionRunner::running() const noexcept { return m_impl->running; }
    bool EvolutionRunner::pause_requested() const noexcept { return m_impl->pause_requested; }
    std::optional<EvolutionUpdate> EvolutionRunner::take_latest_update()
    {
        return m_impl->take_latest_update();
    }
    std::optional<bool> EvolutionRunner::join_if_finished()
    {
        return m_impl->join_if_finished();
    }
}
