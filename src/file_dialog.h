#pragma once

#include <optional>
#include <string>

namespace poly_paint
{
    /** @brief Wraps the native-style ImGui dialog used to choose target images. */
    class ImageFileDialog
    {
    public:
        /** @brief Opens the image selection dialog. */
        void open();
        /** @brief Draws the dialog and returns a selected path when confirmed. */
        [[nodiscard]] std::optional<std::string> draw();
    };
}
