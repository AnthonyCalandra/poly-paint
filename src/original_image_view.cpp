#include "original_image_view.h"

#include "imgui_texture.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>

namespace poly_paint
{
    namespace
    {
        constexpr std::string_view original_image_popup = "Original Image";

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
    }

    void OriginalImageView::draw_preview(const OriginalImageViewModel& image)
    {
        if (!image.valid())
        {
            m_window_open = false;
            return;
        }

        ImGui::Spacing();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float label_height = ImGui::GetTextLineHeight();
        const float label_gap = ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 image_available {
            available.x,
            std::max(0.0f, available.y - label_height - label_gap)};
        const ImVec2 image_size = fit_image(image, image_available);
        if (image_size.x <= 0.0f || image_size.y <= 0.0f)
        {
            return;
        }

        const ImVec2 cursor = ImGui::GetCursorPos();
        const float block_height = label_height + label_gap + image_size.y;
        ImGui::SetCursorPosY(cursor.y + std::max(0.0f, available.y - block_height));
        ImGui::TextUnformatted("Original image");
        ImGui::SetCursorPosX(cursor.x + std::max(0.0f, available.x - image_size.x));
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

        if (!ImGui::IsPopupOpen(original_image_popup.data()))
        {
            ImGui::OpenPopup(original_image_popup.data());
        }

        const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(
            {
                std::min(800.0f, viewport_size.x * 0.8f),
                std::min(600.0f, viewport_size.y * 0.8f)
            },
            ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal(
                original_image_popup.data(),
                &m_window_open,
                ImGuiWindowFlags_NoCollapse))
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
            ImGui::EndPopup();
        }
    }
}
