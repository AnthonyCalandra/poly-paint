#pragma once

#include <BS_thread_pool.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace poly_paint
{
    // A maximising (mu + lambda) evolution strategy. Parents compete with their
    // mutated children for survival, so good solutions are never discarded only
    // because a generation failed to improve them.
    template <typename Individual, typename Fitness = double>
    class EvolutionStrategy
    {
    public:
        struct Settings
        {
            std::size_t parent_count {16};       // mu
            std::size_t offspring_count {64};    // lambda
            std::size_t max_generations {1'000}; // Zero means no generation limit.
            std::chrono::milliseconds time_limit {0}; // Zero means no time limit.
            // Zero uses all available logical processors. The fitness function
            // must be safe to call concurrently when this is greater than one.
            std::size_t worker_count {0};
        };

        using RandomIndividualFactory = std::function<Individual(std::mt19937&)>;
        using MutationFunction = std::function<Individual(const Individual&, std::mt19937&)>;
        using FitnessFunction = std::function<Fitness(const Individual&)>;
        using CompletionPredicate = std::function<bool(const Individual&, const Fitness&)>;
        // Return false to stop after the current generation has been evaluated.
        using ProgressCallback = std::function<bool(std::size_t, const Individual&, const Fitness&)>;

        struct Result
        {
            Individual best_individual;
            Fitness best_fitness;
            std::vector<Individual> parents;
            std::size_t generations_completed {};
            bool stopped_by_completion_condition {};
            bool stopped_by_time_limit {};
            bool stopped_by_progress_callback {};
        };

        EvolutionStrategy(
            Settings settings,
            RandomIndividualFactory make_random_individual,
            MutationFunction mutate,
            FitnessFunction assess_fitness,
            CompletionPredicate is_complete = {},
            ProgressCallback on_generation_completed = {})
            : m_settings(settings)
            , m_make_random_individual(std::move(make_random_individual))
            , m_mutate(std::move(mutate))
            , m_assess_fitness(std::move(assess_fitness))
            , m_is_complete(std::move(is_complete))
            , m_on_generation_completed(std::move(on_generation_completed))
            , m_worker_pool(settings.worker_count > 0
                ? settings.worker_count
                : std::max<std::size_t>(1, std::thread::hardware_concurrency()))
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
            candidates.reserve(maximum_population_count);
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
                for (const ScoredIndividual& parent : parents)
                {
                    candidates.push_back(parent);
                }
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

                std::stable_sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const ScoredIndividual& left, const ScoredIndividual& right)
                    {
                        return left.fitness > right.fitness;
                    });

                parents.clear();
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
        void assess_population(
            std::span<const Individual> population,
            std::span<Fitness> fitnesses) const
        {
            if (m_worker_pool.get_thread_count() <= 1 || population.size() <= 1)
            {
                for (std::size_t index = 0; index < population.size(); ++index)
                {
                    fitnesses[index] = m_assess_fitness(population[index]);
                }
                return;
            }

            const std::size_t block_count = std::min(
                population.size(),
                m_worker_pool.get_thread_count() * 4);
            BS::multi_future<void> completed = m_worker_pool.submit_loop(
                std::size_t {0},
                population.size(),
                [this, &population, &fitnesses](std::size_t index)
                {
                    fitnesses[index] = m_assess_fitness(population[index]);
                },
                block_count);
            completed.get();
        }

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
        mutable BS::thread_pool<> m_worker_pool;
    };
}
