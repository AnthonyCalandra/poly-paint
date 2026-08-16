#pragma once

namespace poly_paint
{
    struct PanelLayout
    {
        float work_x {};
        float work_y {};
        float work_height {};
        float canvas_width {};
        float controls_width {};
    };

    [[nodiscard]] PanelLayout calculate_panel_layout();
}
