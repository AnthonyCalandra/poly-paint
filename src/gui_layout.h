#pragma once

namespace poly_paint
{
    /** @brief Pixel-space dimensions and positions for the application's panels. */
    struct PanelLayout
    {
        /** @brief X coordinate of the usable work area. */
        float work_x {};
        /** @brief Y coordinate of the usable work area. */
        float work_y {};
        /** @brief Height of both main panels. */
        float work_height {};
        /** @brief Width allocated to the canvas panel. */
        float canvas_width {};
        /** @brief Width allocated to the controls panel. */
        float controls_width {};
    };

    /** @brief Calculates panel bounds for the current ImGui viewport. */
    [[nodiscard]] PanelLayout calculate_panel_layout();
}
