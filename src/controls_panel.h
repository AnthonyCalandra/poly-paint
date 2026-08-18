#pragma once

#include "evolution_runner.h"
#include "gui_layout.h"
#include "original_image_view.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace poly_paint
{
    /** @brief Data displayed by the controls panel for the current application state. */
    struct ControlsPanelModel
    {
        /** @brief Most recently displayed evolution generation. */
        std::size_t current_generation {};
        /** @brief Best similarity score reported by the evolution. */
        double best_score {};
        /** @brief Reference-image texture data for the preview controls. */
        OriginalImageViewModel original_image;
        /** @brief Whether a target image is available for evolution. */
        bool has_target {};
        /** @brief Whether an evolution job is active. */
        bool evolution_running {};
        /** @brief Whether an active evolution has been asked to pause. */
        bool evolution_pause_requested {};
        /** @brief Status text shown beneath the controls. */
        std::string_view status_message;
    };

    /** @brief User actions emitted while drawing a controls panel frame. */
    struct ControlsPanelActions
    {
        /** @brief Requests opening the image dialog. */
        bool open_image {};
        /** @brief Requests resuming a paused run. */
        bool resume_evolution {};
        /** @brief Requests pausing the active run. */
        bool pause_evolution {};
        /** @brief Requests stopping the active run. */
        bool stop_evolution {};
        /** @brief Settings for a newly requested run, if any. */
        std::optional<EvolutionRunSettings> start_evolution;
        /** @brief Requested PNG output path, if any. */
        std::optional<std::string> save_path;
    };

    /** @brief Renders and owns state for the application's controls panel. */
    class ControlsPanel
    {
    public:
        /** @brief Constructs controls with the application's default settings. */
        ControlsPanel();

        /** @brief Draws the controls and returns actions requested by the user. */
        [[nodiscard]] ControlsPanelActions draw(
            const PanelLayout& layout,
            const ControlsPanelModel& model);

        /** @brief Returns the current canvas zoom multiplier. */
        [[nodiscard]] float zoom() const noexcept { return m_zoom; }

    private:
        float m_zoom {1.0f};
        std::array<char, 32> m_generation_limit_text {};
        std::optional<std::size_t> m_generation_limit;
        std::string m_generation_limit_error;
        std::array<char, 4> m_parent_count_text {};
        std::optional<std::size_t> m_parent_count {5};
        std::string m_parent_count_error;
        std::array<char, 4> m_offspring_count_text {};
        std::optional<std::size_t> m_offspring_count {1};
        std::string m_offspring_count_error;
        int m_initial_population_mode {};
        int m_polygon_count_preset {};
        std::array<char, 260> m_export_path {};
        OriginalImageView m_original_image_view;
    };
}
