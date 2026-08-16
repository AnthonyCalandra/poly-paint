#pragma once

#include <glad/gl.h>

#include <cstddef>

namespace poly_paint
{
    struct OriginalImageViewModel
    {
        GLuint texture {};
        std::size_t width {};
        std::size_t height {};

        [[nodiscard]] bool valid() const noexcept
        {
            return texture != 0 && width != 0 && height != 0;
        }
    };

    class OriginalImageView
    {
    public:
        void draw_preview(const OriginalImageViewModel& image);
        void draw_window(const OriginalImageViewModel& image);

    private:
        bool m_window_open {};
    };
}
