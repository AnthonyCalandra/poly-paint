#pragma once

#include <optional>
#include <string>

namespace poly_paint
{
    class ImageFileDialog
    {
    public:
        void open();
        [[nodiscard]] std::optional<std::string> draw();
    };
}
