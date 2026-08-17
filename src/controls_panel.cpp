#include "controls_panel.h"

#include <imgui.h>

#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

namespace poly_paint
{
    namespace
    {
        constexpr ImGuiWindowFlags fixed_panel_flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;
        constexpr std::array<std::size_t, 4> polygon_count_presets {50, 100, 500, 1'000};

        void draw_text(std::string_view text)
        {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void set_text_input_width(std::string_view widest_expected_value)
        {
            const float text_width = ImGui::CalcTextSize(
                widest_expected_value.data(),
                widest_expected_value.data() + widest_expected_value.size()).x;
            const float horizontal_padding = ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(text_width + horizontal_padding + 4.0f);
        }

        void parse_generation_limit(
            std::string_view text,
            std::optional<std::size_t>& generation_limit,
            std::string& error_message)
        {
            if (text.empty())
            {
                generation_limit.reset();
                error_message.clear();
                return;
            }

            unsigned long long parsed_value = 0;
            const char* const end = text.data() + text.size();
            const auto parse_result = std::from_chars(text.data(), end, parsed_value);
            if (parse_result.ec != std::errc {} || parse_result.ptr != end ||
                parsed_value == 0 || parsed_value > std::numeric_limits<std::size_t>::max())
            {
                generation_limit.reset();
                error_message = "Enter a whole number greater than zero, or leave this blank.";
                return;
            }

            generation_limit = static_cast<std::size_t>(parsed_value);
            error_message.clear();
        }

        void draw_polygon_count_selector(int& selected_preset)
        {
            const std::string selected_count = std::format(
                "{}", polygon_count_presets[static_cast<std::size_t>(selected_preset)]);
            ImGui::SliderInt(
                "Polygons",
                &selected_preset,
                0,
                static_cast<int>(polygon_count_presets.size() - 1),
                selected_count.c_str(),
                ImGuiSliderFlags_NoInput);

            const ImVec2 slider_min = ImGui::GetItemRectMin();
            const ImVec2 slider_max = ImGui::GetItemRectMax();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            for (std::size_t index = 0; index < polygon_count_presets.size(); ++index)
            {
                const std::string label = std::format("{}", polygon_count_presets[index]);
                const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
                const float fraction = static_cast<float>(index) /
                    static_cast<float>(polygon_count_presets.size() - 1);
                const float center_x = slider_min.x + (slider_max.x - slider_min.x) * fraction;
                draw_list->AddText(
                    ImVec2(center_x - label_size.x * 0.5f, slider_max.y + 3.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    label.c_str());
            }
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() + 3.0f));
        }

        void draw_generation_limit(
            std::array<char, 32>& text,
            std::optional<std::size_t>& generation_limit,
            std::string& error_message)
        {
            set_text_input_width("1000000");
            ImGui::InputText(
                "Generation limit",
                text.data(),
                text.size(),
                ImGuiInputTextFlags_CharsDecimal);
            parse_generation_limit(std::string_view {text.data()}, generation_limit, error_message);
            if (!error_message.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                draw_text(error_message);
                ImGui::PopStyleColor();
            }
            else if (generation_limit)
            {
                draw_text(std::format(
                    "Run will stop after {} generations.", *generation_limit));
            }
            else
            {
                ImGui::TextUnformatted("No generation limit.");
            }
        }

        void draw_population_count(
            const char* label,
            std::string_view name,
            std::array<char, 4>& text,
            std::optional<std::size_t>& value,
            std::string& error_message)
        {
            set_text_input_width("50");
            ImGui::InputText(
                label,
                text.data(),
                text.size(),
                ImGuiInputTextFlags_CharsDecimal);

            unsigned long long parsed_value = 0;
            const std::string_view input {text.data()};
            const char* const end = input.data() + input.size();
            const auto parse_result = std::from_chars(input.data(), end, parsed_value);
            if (input.empty() || parse_result.ec != std::errc {} || parse_result.ptr != end ||
                parsed_value < EvolutionRunSettings::minimum_population_count ||
                parsed_value > EvolutionRunSettings::maximum_population_count)
            {
                value.reset();
                error_message = std::format(
                    "{} must be a whole number from {} to {}.",
                    name,
                    EvolutionRunSettings::minimum_population_count,
                    EvolutionRunSettings::maximum_population_count);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                draw_text(error_message);
                ImGui::PopStyleColor();
                return;
            }

            value = static_cast<std::size_t>(parsed_value);
            error_message.clear();
        }
    }

    ControlsPanel::ControlsPanel()
    {
        constexpr std::string_view default_export_path = "output.png";
        constexpr std::string_view default_parent_count = "5";
        constexpr std::string_view default_offspring_count = "1";
        std::ranges::copy(default_export_path, m_export_path.begin());
        std::ranges::copy(default_parent_count, m_parent_count_text.begin());
        std::ranges::copy(default_offspring_count, m_offspring_count_text.begin());
    }

    ControlsPanelActions ControlsPanel::draw(
        const PanelLayout& layout,
        const ControlsPanelModel& model)
    {
        ControlsPanelActions actions;
        ImGui::SetNextWindowPos(ImVec2(layout.work_x + layout.canvas_width, layout.work_y));
        ImGui::SetNextWindowSize(ImVec2(layout.controls_width, layout.work_height));
        ImGui::Begin("Controls", nullptr, fixed_panel_flags);

        if (model.evolution_running)
        {
            ImGui::BeginDisabled();
        }
        actions.open_image = ImGui::Button("Open image...");
        if (model.evolution_running)
        {
            ImGui::EndDisabled();
        }
        ImGui::SliderFloat("Zoom", &m_zoom, 0.5f, 6.0f, "%.2fx");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            m_zoom = 1.0f;
        }
        ImGui::Separator();

        draw_text(std::format("Current generation: {}", model.current_generation));
        draw_generation_limit(
            m_generation_limit_text, m_generation_limit, m_generation_limit_error);

        ImGui::Separator();
        ImGui::TextUnformatted("Initial population");
        if (model.evolution_running)
        {
            ImGui::BeginDisabled();
        }
        ImGui::RadioButton("Randomized polygons", &m_initial_population_mode, 0);
        ImGui::RadioButton("Best guess", &m_initial_population_mode, 1);
        draw_polygon_count_selector(m_polygon_count_preset);
        draw_population_count(
            "Mu (parents)",
            "Mu",
            m_parent_count_text,
            m_parent_count,
            m_parent_count_error);
        draw_population_count(
            "Lambda (offspring)",
            "Lambda",
            m_offspring_count_text,
            m_offspring_count,
            m_offspring_count_error);
        if (model.evolution_running)
        {
            ImGui::EndDisabled();
        }

        const bool population_counts_valid = m_parent_count && m_offspring_count;
        const bool can_start = model.evolution_running
            ? model.evolution_pause_requested
            : model.has_target && population_counts_valid;
        if (!can_start)
        {
            ImGui::BeginDisabled();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.21f, 0.68f, 0.37f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.42f, 0.22f, 1.0f));
        if (ImGui::Button(
                model.evolution_pause_requested ? "Resume Evolution" : "Start Evolution",
                ImVec2(-1.0f, 0.0f)))
        {
            if (model.evolution_running)
            {
                actions.resume_evolution = true;
            }
            else
            {
                actions.start_evolution = EvolutionRunSettings {
                    .maximum_generations = m_generation_limit.value_or(0),
                    .polygon_count =
                        polygon_count_presets[static_cast<std::size_t>(m_polygon_count_preset)],
                    .parent_count = *m_parent_count,
                    .offspring_count = *m_offspring_count,
                    .initial_population = m_initial_population_mode == 1
                        ? InitialPopulationMode::best_guess
                        : InitialPopulationMode::randomized
                };
            }
        }
        ImGui::PopStyleColor(3);
        if (!can_start)
        {
            ImGui::EndDisabled();
            if (!model.has_target)
            {
                ImGui::TextDisabled("Open an image to enable evolution.");
            }
        }

        if (model.evolution_running)
        {
            if (!model.evolution_pause_requested)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.48f, 0.12f, 1.0f));
                actions.pause_evolution = ImGui::Button("Pause Evolution", ImVec2(-1.0f, 0.0f));
                ImGui::PopStyleColor();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
            actions.stop_evolution = ImGui::Button("Stop Evolution", ImVec2(-1.0f, 0.0f));
            ImGui::PopStyleColor();
            draw_text(std::format("Best similarity: {:.4f}", model.best_score));
        }

        ImGui::InputText("Export path", m_export_path.data(), m_export_path.size());
        if (ImGui::Button("Save PNG"))
        {
            actions.save_path = std::string {m_export_path.data()};
        }
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        const char* const status_begin = model.status_message.empty()
            ? ""
            : model.status_message.data();
        ImGui::TextUnformatted(
            status_begin,
            status_begin + model.status_message.size());
        ImGui::PopTextWrapPos();
        m_original_image_view.draw_preview(model.original_image);
        ImGui::End();
        m_original_image_view.draw_window(model.original_image);
        return actions;
    }
}
