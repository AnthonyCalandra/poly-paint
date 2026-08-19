#pragma once

#include <glad/gl.h>

#include <cstddef>

namespace poly_paint
{
    /** @brief Texture and dimensions needed to display the loaded reference image. */
    struct OriginalImageViewModel
    {
        /** @brief OpenGL texture handle for the reference image. */
        GLuint texture {};
        /** @brief Reference image width in pixels. */
        std::size_t width {};
        /** @brief Reference image height in pixels. */
        std::size_t height {};

        /** @brief Returns whether this model refers to a non-empty texture. */
        [[nodiscard]] bool valid() const noexcept
        {
            return texture != 0 && width != 0 && height != 0;
        }
    };

    /** @brief Draws preview and full-size views of the reference image. */
    class OriginalImageView
    {
    public:
        /** @brief Draws an inline preview in the controls panel. */
        void draw_preview(const OriginalImageViewModel& image);
        /** @brief Draws the optional full-size image modal. */
        void draw_window(const OriginalImageViewModel& image);

    private:
        bool m_window_open {};
    };
}
