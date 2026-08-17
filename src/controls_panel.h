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
    struct ControlsPanelModel
    {
        std::size_t current_generation {};
        double best_score {};
        OriginalImageViewModel original_image;
        bool has_target {};
        bool evolution_running {};
        bool evolution_pause_requested {};
        std::string_view status_message;
    };

    struct ControlsPanelActions
    {
        bool open_image {};
        bool resume_evolution {};
        bool pause_evolution {};
        bool stop_evolution {};
        std::optional<EvolutionRunSettings> start_evolution;
        std::optional<std::string> save_path;
    };

    class ControlsPanel
    {
    public:
        ControlsPanel();

        [[nodiscard]] ControlsPanelActions draw(
            const PanelLayout& layout,
            const ControlsPanelModel& model);

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
