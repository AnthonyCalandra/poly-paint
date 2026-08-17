#include "canvas_panel.h"

#include "imgui_texture.h"

#include <imgui.h>

#include <format>
#include <limits>
#include <string>

namespace poly_paint
{
    namespace
    {
        constexpr ImGuiWindowFlags fixed_panel_flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;

        void draw_performance_overlay()
        {
            const std::string fps_text =
                std::format("{:.0f} FPS", ImGui::GetIO().Framerate);

            const ImVec2 image_min = ImGui::GetItemRectMin();
            const ImVec2 image_max = ImGui::GetItemRectMax();
            const ImVec2 text_size = ImGui::CalcTextSize(fps_text.c_str());
            const ImVec2 text_position {image_max.x - text_size.x - 12.0f, image_min.y + 10.0f};
            ImDrawList* overlay = ImGui::GetWindowDrawList();
            overlay->AddRectFilled(
                ImVec2(text_position.x - 6.0f, text_position.y - 4.0f),
                ImVec2(text_position.x + text_size.x + 6.0f, text_position.y + text_size.y + 4.0f),
                IM_COL32(0, 0, 0, 180),
                4.0f);
            constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();
            overlay->AddText(
                text_position,
                IM_COL32(maximum_channel, maximum_channel, maximum_channel, maximum_channel),
                fps_text.c_str());
        }
    }

    void draw_canvas_panel(
        const PanelLayout& layout,
        Canvas& canvas,
        GLuint texture,
        float zoom,
        bool show_performance_overlay)
    {
        ImGui::SetNextWindowPos(ImVec2(layout.work_x, layout.work_y));
        ImGui::SetNextWindowSize(ImVec2(layout.canvas_width, layout.work_height));
        ImGui::Begin("Canvas", nullptr, fixed_panel_flags);

        canvas.upload_to_texture(texture);
        const ImVec2 image_size {
            static_cast<float>(canvas.width()) * zoom,
            static_cast<float>(canvas.height()) * zoom
        };
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > image_size.x)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - image_size.x) * 0.5f);
        }
        if (available.y > image_size.y)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (available.y - image_size.y) * 0.5f);
        }
        ImGui::Image(to_imgui_texture(texture), image_size);
        if (show_performance_overlay)
        {
            draw_performance_overlay();
        }
        ImGui::End();
    }
}
