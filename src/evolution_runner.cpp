#include "evolution_runner.h"

#include "cooperative_evolution.h"
#include "polygon_evolution.h"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace poly_paint
{
    namespace
    {
        void validate_settings(const EvolutionRunSettings& settings)
        {
            if (settings.parent_count < EvolutionRunSettings::minimum_population_count ||
                settings.parent_count > EvolutionRunSettings::maximum_population_count ||
                settings.offspring_count < EvolutionRunSettings::minimum_population_count ||
                settings.offspring_count > EvolutionRunSettings::maximum_population_count)
            {
                throw std::invalid_argument(
                    "Mu and lambda must each be between one and fifty.");
            }
        }

        [[nodiscard]] std::vector<ContrastSeed> make_initial_seeds(
            const ImageSimilarityScorer& target,
            const EvolutionRunSettings& settings)
        {
            if (settings.initial_population != InitialPopulationMode::best_guess)
            {
                return {};
            }

            return find_high_contrast_seeds(
                target.target_rgba(), target.width(), target.height(), settings.polygon_count);
        }

        class BackgroundEvolutionJob
        {
        public:
            BackgroundEvolutionJob(
                ImageSimilarityScorer target,
                EvolutionRunSettings settings,
                std::vector<ContrastSeed> seeds,
                detail::EvolutionRunControl& control,
                detail::EvolutionUpdateMailbox& updates,
                std::atomic<bool>& running)
                : m_target(std::move(target))
                , m_settings(settings)
                , m_seeds(std::move(seeds))
                , m_control(control)
                , m_updates(updates)
                , m_running(running)
            {
            }

            void operator()()
            {
                detail::run_cooperative_evolution(
                    m_target, m_settings, m_seeds, m_control, m_updates);
                m_running = false;
            }

        private:
            ImageSimilarityScorer m_target;
            EvolutionRunSettings m_settings;
            std::vector<ContrastSeed> m_seeds;
            detail::EvolutionRunControl& m_control;
            detail::EvolutionUpdateMailbox& m_updates;
            std::atomic<bool>& m_running;
        };
    }

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
            if (m_running)
            {
                throw std::logic_error("Cannot start an evolution job while another job is running.");
            }
            if (m_worker.joinable())
            {
                m_worker.join();
            }

            validate_settings(settings);
            std::vector<ContrastSeed> seeds = make_initial_seeds(target, settings);
            m_updates.reset();
            m_control.reset();
            m_running = true;
            try
            {
                m_worker = std::thread(BackgroundEvolutionJob {
                    target,
                    settings,
                    std::move(seeds),
                    m_control,
                    m_updates,
                    m_running});
            }
            catch (...)
            {
                m_running = false;
                throw;
            }
        }

        void request_pause() noexcept { m_control.request_pause(); }
        void resume() noexcept { m_control.resume(); }
        void request_stop() noexcept { m_control.request_stop(); }

        void stop_and_wait() noexcept
        {
            request_stop();
            if (m_worker.joinable())
            {
                m_worker.join();
            }
        }

        [[nodiscard]] bool running() const noexcept { return m_running; }
        [[nodiscard]] bool pause_requested() const noexcept
        {
            return m_control.pause_requested();
        }

        [[nodiscard]] std::optional<EvolutionUpdate> take_latest_update()
        {
            return m_updates.take_latest();
        }

        [[nodiscard]] std::optional<bool> join_if_finished()
        {
            if (m_running || !m_worker.joinable())
            {
                return std::nullopt;
            }
            const bool stopped = m_control.cancel_requested();
            m_worker.join();
            return stopped;
        }

    private:
        std::thread m_worker;
        std::atomic<bool> m_running {false};
        detail::EvolutionRunControl m_control;
        detail::EvolutionUpdateMailbox m_updates;
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
    bool EvolutionRunner::running() const noexcept { return m_impl->running(); }
    bool EvolutionRunner::pause_requested() const noexcept { return m_impl->pause_requested(); }
    std::optional<EvolutionUpdate> EvolutionRunner::take_latest_update()
    {
        return m_impl->take_latest_update();
    }
    std::optional<bool> EvolutionRunner::join_if_finished()
    {
        return m_impl->join_if_finished();
    }
}
