#include "original_image_view.h"

#include "imgui_texture.h"

#include <imgui.h>

#include <algorithm>

namespace poly_paint
{
    namespace
    {
        [[nodiscard]] ImVec2 fit_image(
            const OriginalImageViewModel& image,
            ImVec2 available)
        {
            if (!image.valid() || available.x <= 0.0f || available.y <= 0.0f)
            {
                return {};
            }

            const float width = static_cast<float>(image.width);
            const float height = static_cast<float>(image.height);
            const float scale = std::min(available.x / width, available.y / height);
            return {width * scale, height * scale};
        }

        void align_bottom_right(ImVec2 available, ImVec2 item_size)
        {
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({
                cursor.x + std::max(0.0f, available.x - item_size.x),
                cursor.y + std::max(0.0f, available.y - item_size.y)
            });
        }
    }

    void OriginalImageView::draw_preview(const OriginalImageViewModel& image)
    {
        if (!image.valid())
        {
            m_window_open = false;
            return;
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Original image");
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 image_size = fit_image(image, available);
        if (image_size.x <= 0.0f || image_size.y <= 0.0f)
        {
            return;
        }

        align_bottom_right(available, image_size);
        ImGui::Image(to_imgui_texture(image.texture), image_size);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetItemTooltip("Double-click to open the original image.");
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_window_open = true;
            }
        }
    }

    void OriginalImageView::draw_window(const OriginalImageViewModel& image)
    {
        if (!m_window_open || !image.valid())
        {
            return;
        }

        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(
            {
                std::min(800.0f, viewport_size.x * 0.8f),
                std::min(600.0f, viewport_size.y * 0.8f)
            },
            ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Original Image", &m_window_open))
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 image_size = fit_image(image, available);
            if (image_size.x > 0.0f && image_size.y > 0.0f)
            {
                const ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos({
                    cursor.x + std::max(0.0f, (available.x - image_size.x) * 0.5f),
                    cursor.y + std::max(0.0f, (available.y - image_size.y) * 0.5f)
                });
                ImGui::Image(to_imgui_texture(image.texture), image_size);
            }
        }
        ImGui::End();
    }
}
