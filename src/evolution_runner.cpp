#include "evolution_runner.h"

#include "evolution_strategy.h"
#include "image_dimensions.h"
#include "polygon_evolution.h"
#include "polygon_rasterizer.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
            std::size_t maximum_generations,
            std::size_t polygon_count,
            InitialPopulationMode initial_population)
        {
            if (running)
            {
                throw std::logic_error("Cannot start an evolution job while another job is running.");
            }
            if (worker.joinable())
            {
                worker.join();
            }

            std::vector<ContrastSeed> seeds;
            if (initial_population == InitialPopulationMode::best_guess)
            {
                seeds = find_high_contrast_seeds(
                    target.target_rgba(), target.width(), target.height(), polygon_count);
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
                maximum_generations,
                polygon_count,
                initial_population,
                seeds = std::move(seeds)]() mutable
            {
                run(
                    target,
                    maximum_generations,
                    polygon_count,
                    initial_population,
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
            std::size_t maximum_generations,
            std::size_t polygon_count,
            InitialPopulationMode initial_population,
            std::vector<ContrastSeed> seeds)
        {
            using Strategy = EvolutionStrategy<PolygonCollection>;
            Strategy::Settings settings;
            settings.parent_count = 5;
            settings.offspring_count = 20;
            settings.max_generations = maximum_generations;

            const std::size_t image_byte_count =
                checked_rgba_byte_count(target.width(), target.height());
            std::vector<std::uint8_t> display_image(image_byte_count);

            Strategy strategy(
                settings,
                [initial_population,
                 seeds = std::move(seeds),
                 width = target.width(),
                 height = target.height(),
                 polygon_count](std::mt19937& random_engine)
                {
                    if (initial_population == InitialPopulationMode::best_guess)
                    {
                        return make_best_guess_polygon_collection(
                            seeds, width, height, polygon_count, random_engine);
                    }
                    return make_random_polygon_collection(random_engine, polygon_count);
                },
                mutate_polygon_collection,
                [&target](const PolygonCollection& candidate)
                {
                    constexpr std::size_t tile_row_count = 64;
                    thread_local std::vector<std::uint8_t> tile_storage;
                    const std::size_t maximum_tile_size =
                        checked_rgba_byte_count(
                            target.width(), std::min(tile_row_count, target.height()));
                    tile_storage.resize(maximum_tile_size);

                    std::uint64_t total_difference = 0;
                    for (std::size_t first_row = 0;
                         first_row < target.height();
                         first_row += tile_row_count)
                    {
                        const std::size_t row_count =
                            std::min(tile_row_count, target.height() - first_row);
                        const std::size_t tile_size =
                            checked_rgba_byte_count(target.width(), row_count);
                        const std::span<std::uint8_t> tile =
                            std::span<std::uint8_t> {tile_storage}.first(tile_size);
                        rasterize_rows_into(
                            candidate, target.width(), target.height(), first_row, row_count, tile);
                        total_difference += target.difference_for_rows(tile, first_row, row_count);
                    }
                    return target.score_from_total_difference(total_difference);
                },
                {},
                [this, &target, &display_image](
                    std::size_t generation,
                    const PolygonCollection& best_candidate,
                    const double& score)
                {
                    rasterize_into(
                        best_candidate, target.width(), target.height(), display_image);
                    {
                        std::lock_guard lock(update_mutex);
                        latest_update = EvolutionUpdate {display_image, generation, score};
                    }
                    if (pause_requested)
                    {
                        std::unique_lock lock(pause_mutex);
                        pause_condition.wait(lock, [this]
                        {
                            return !pause_requested.load() || cancel_requested.load();
                        });
                    }
                    return !cancel_requested.load();
                });

            std::mt19937 random_engine(std::random_device {}());
            [[maybe_unused]] Strategy::Result result = strategy.run(random_engine);
        }
    };

    EvolutionRunner::EvolutionRunner()
        : m_impl(std::make_unique<Impl>())
    {
    }

    EvolutionRunner::~EvolutionRunner() = default;

    void EvolutionRunner::start(
        const ImageSimilarityScorer& target,
        std::size_t maximum_generations,
        std::size_t polygon_count,
        InitialPopulationMode initial_population)
    {
        m_impl->start(target, maximum_generations, polygon_count, initial_population);
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
