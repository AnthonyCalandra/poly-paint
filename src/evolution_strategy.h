#pragma once

#include <BS_thread_pool.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace poly_paint
{
    /** @brief A maximizing @f$(\mu + \lambda)@f$ evolution strategy.
     *  @details Parents compete with their mutated children, so a non-improving
     *  generation cannot discard an already good solution.
     */
    template <typename Individual, typename Fitness = double>
    class EvolutionStrategy
    {
    public:
        /** @brief Parameters that control population size, duration, and parallelism. */
        struct Settings
        {
            /** @brief Number of selected parents (@f$\mu@f$); must be nonzero. */
            std::size_t parent_count {16};
            /** @brief Number of mutated children per generation (@f$\lambda@f$). */
            std::size_t offspring_count {64};
            /** @brief Generation limit; zero permits an unlimited run. */
            std::size_t max_generations {1'000};
            /** @brief Wall-clock limit; zero disables time-based stopping. */
            std::chrono::milliseconds time_limit {0};
            /** @brief Fitness worker count; zero uses available logical processors.
             *  @details The fitness function must be thread-safe when this exceeds one.
             */
            std::size_t worker_count {0};
        };

        /** @brief Creates a random individual using the supplied random engine. */
        using RandomIndividualFactory = std::function<Individual(std::mt19937&)>;
        /** @brief Produces one mutation of a parent using the supplied random engine. */
        using MutationFunction = std::function<Individual(const Individual&, std::mt19937&)>;
        /** @brief Computes an individual's fitness; higher values are better. */
        using FitnessFunction = std::function<Fitness(const Individual&)>;
        /** @brief Tests whether an individual satisfies an optional completion condition. */
        using CompletionPredicate = std::function<bool(const Individual&, const Fitness&)>;
        /** @brief Receives each completed generation and returns whether to continue. */
        using ProgressCallback = std::function<bool(std::size_t, const Individual&, const Fitness&)>;
        /** @brief A previously scored individual that may migrate into a population. */
        using Migrant = std::pair<Individual, Fitness>;
        /** @brief Supplies an optional scored migrant for a generation. */
        using MigrationSource = std::function<std::optional<Migrant>(std::size_t)>;

        /** @brief Final population, best candidate, and reason the run ended. */
        struct Result
        {
            /** @brief Highest-fitness individual encountered. */
            Individual best_individual;
            /** @brief Fitness of @ref best_individual. */
            Fitness best_fitness;
            /** @brief Selected parent population from the final generation. */
            std::vector<Individual> parents;
            /** @brief Number of fully evaluated generations. */
            std::size_t generations_completed {};
            /** @brief Whether the completion predicate stopped the run. */
            bool stopped_by_completion_condition {};
            /** @brief Whether the time limit stopped the run. */
            bool stopped_by_time_limit {};
            /** @brief Whether the progress callback stopped the run. */
            bool stopped_by_progress_callback {};
        };

        /** @brief Constructs a strategy from its settings and behavior callbacks.
         *  @throws std::invalid_argument if parent count or required callbacks are invalid.
         */
        EvolutionStrategy(
            Settings settings,
            RandomIndividualFactory make_random_individual,
            MutationFunction mutate,
            FitnessFunction assess_fitness,
            CompletionPredicate is_complete = {},
            ProgressCallback on_generation_completed = {},
            MigrationSource migrant_source = {})
            : m_settings(settings)
            , m_make_random_individual(std::move(make_random_individual))
            , m_mutate(std::move(mutate))
            , m_assess_fitness(std::move(assess_fitness))
            , m_is_complete(std::move(is_complete))
            , m_on_generation_completed(std::move(on_generation_completed))
            , m_migrant_source(std::move(migrant_source))
            , m_worker_pool(make_worker_pool(settings.worker_count))
        {
            if (m_settings.parent_count == 0)
            {
                throw std::invalid_argument("EvolutionStrategy parent_count must be greater than zero.");
            }
            if (!m_make_random_individual || !m_mutate || !m_assess_fitness)
            {
                throw std::invalid_argument("EvolutionStrategy requires initialization, mutation, and fitness functions.");
            }
        }

        /** @brief Evolves a population using @p random_engine and returns its result. */
        [[nodiscard]] Result run(std::mt19937& random_engine) const
        {
            const std::size_t initial_population_count = std::max(
                m_settings.parent_count,
                m_settings.offspring_count);
            const std::size_t maximum_population_count = std::max(
                initial_population_count,
                m_settings.parent_count + m_settings.offspring_count);

            std::vector<Individual> unevaluated;
            unevaluated.reserve(initial_population_count);
            for (std::size_t index = 0; index < initial_population_count; ++index)
            {
                unevaluated.push_back(m_make_random_individual(random_engine));
            }

            const auto started_at = std::chrono::steady_clock::now();
            std::optional<Individual> best_individual;
            std::optional<Fitness> best_fitness;
            std::vector<ScoredIndividual> parents;
            parents.reserve(m_settings.parent_count);
            std::vector<ScoredIndividual> candidates;
            candidates.reserve(maximum_population_count + (m_migrant_source ? 1 : 0));
            std::vector<Fitness> fitnesses;
            fitnesses.reserve(initial_population_count);
            std::size_t generations_completed = 0;
            bool stopped_by_completion_condition = false;
            bool stopped_by_time_limit = false;
            bool stopped_by_progress_callback = false;

            for (std::size_t generation = 0;
                 m_settings.max_generations == 0 || generation < m_settings.max_generations;
                 ++generation)
            {
                fitnesses.resize(unevaluated.size());
                assess_population(unevaluated, fitnesses);

                candidates.clear();
                candidates.swap(parents);
                for (std::size_t index = 0; index < unevaluated.size(); ++index)
                {
                    Individual& individual = unevaluated[index];
                    Fitness& fitness = fitnesses[index];
                    if (!best_fitness || fitness > *best_fitness)
                    {
                        best_individual = individual;
                        best_fitness = fitness;
                    }
                    candidates.push_back({std::move(individual), std::move(fitness)});
                }
                if (m_migrant_source)
                {
                    std::optional<Migrant> migrant = m_migrant_source(generation + 1);
                    if (migrant)
                    {
                        if (!best_fitness || migrant->second > *best_fitness)
                        {
                            best_individual = migrant->first;
                            best_fitness = migrant->second;
                        }
                        candidates.push_back(
                            {std::move(migrant->first), std::move(migrant->second)});
                    }
                }

                std::stable_sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const ScoredIndividual& left, const ScoredIndividual& right)
                    {
                        return left.fitness > right.fitness;
                    });

                for (std::size_t index = 0; index < m_settings.parent_count; ++index)
                {
                    parents.push_back(std::move(candidates[index]));
                }
                generations_completed = generation + 1;

                if (m_on_generation_completed &&
                    !m_on_generation_completed(generations_completed, *best_individual, *best_fitness))
                {
                    stopped_by_progress_callback = true;
                    break;
                }
                if (m_is_complete && m_is_complete(*best_individual, *best_fitness))
                {
                    stopped_by_completion_condition = true;
                    break;
                }
                if (m_settings.time_limit.count() > 0 &&
                    std::chrono::steady_clock::now() - started_at >= m_settings.time_limit)
                {
                    stopped_by_time_limit = true;
                    break;
                }
                if (m_settings.max_generations > 0 && generation + 1 == m_settings.max_generations)
                {
                    break;
                }

                // Keep mu selected parents, then produce exactly lambda children
                // from randomly selected parents.
                unevaluated.clear();
                unevaluated.reserve(m_settings.offspring_count);
                std::uniform_int_distribution<std::size_t> parent_index(0, parents.size() - 1);
                for (std::size_t child = 0; child < m_settings.offspring_count; ++child)
                {
                    const Individual& parent = parents[parent_index(random_engine)].individual;
                    unevaluated.push_back(m_mutate(parent, random_engine));
                }
            }

            std::vector<Individual> result_parents;
            result_parents.reserve(parents.size());
            for (ScoredIndividual& parent : parents)
            {
                result_parents.push_back(std::move(parent.individual));
            }

            return {
                std::move(*best_individual),
                std::move(*best_fitness),
                std::move(result_parents),
                generations_completed,
                stopped_by_completion_condition,
                stopped_by_time_limit,
                stopped_by_progress_callback
            };
        }

    private:
        /** @brief Creates a worker pool when parallel fitness evaluation is requested. */
        [[nodiscard]] static std::unique_ptr<BS::thread_pool<>> make_worker_pool(
            std::size_t requested_worker_count)
        {
            const std::size_t worker_count = requested_worker_count > 0
                ? requested_worker_count
                : std::max<std::size_t>(1, std::thread::hardware_concurrency());
            if (worker_count <= 1)
            {
                return {};
            }
            return std::make_unique<BS::thread_pool<>>(worker_count);
        }

        /** @brief Evaluates every candidate, serially or in parallel as configured. */
        void assess_population(
            std::span<const Individual> population,
            std::span<Fitness> fitnesses) const
        {
            if (!m_worker_pool || population.size() <= 1)
            {
                for (std::size_t index = 0; index < population.size(); ++index)
                {
                    fitnesses[index] = m_assess_fitness(population[index]);
                }
                return;
            }

            const std::size_t block_count = std::min(
                population.size(),
                m_worker_pool->get_thread_count() * 4);
            BS::multi_future<void> completed = m_worker_pool->submit_loop(
                std::size_t {0},
                population.size(),
                [this, &population, &fitnesses](std::size_t index)
                {
                    fitnesses[index] = m_assess_fitness(population[index]);
                },
                block_count);
            completed.get();
        }

        /** @brief An individual paired with its already computed fitness. */
        struct ScoredIndividual
        {
            Individual individual;
            Fitness fitness;
        };

        Settings m_settings;
        RandomIndividualFactory m_make_random_individual;
        MutationFunction m_mutate;
        FitnessFunction m_assess_fitness;
        CompletionPredicate m_is_complete;
        ProgressCallback m_on_generation_completed;
        MigrationSource m_migrant_source;
        mutable std::unique_ptr<BS::thread_pool<>> m_worker_pool;
    };
}
