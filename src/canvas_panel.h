#pragma once

#include "canvas.h"
#include "gui_layout.h"

namespace poly_paint
{
    void draw_canvas_panel(
        const PanelLayout& layout,
        Canvas& canvas,
        GLuint texture,
        float zoom,
        bool show_performance_overlay);
}
