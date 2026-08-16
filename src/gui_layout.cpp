#include "gui_layout.h"

#include <imgui.h>

#include <algorithm>

namespace poly_paint
{
    PanelLayout calculate_panel_layout()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float controls_width = std::clamp(viewport->WorkSize.x * 0.28f, 280.0f, 380.0f);
        return {
            viewport->WorkPos.x,
            viewport->WorkPos.y,
            viewport->WorkSize.y,
            std::max(1.0f, viewport->WorkSize.x - controls_width),
            controls_width
        };
    }
}
