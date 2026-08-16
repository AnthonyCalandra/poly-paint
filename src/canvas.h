#pragma once

#include "polygon.h"

#include <glad/gl.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace poly_paint
{
    class Canvas
    {
    public:
        Canvas(std::size_t width, std::size_t height);

        void clear(RgbaColor color);
        void fill_gradient();
        void fill_noise();
        bool load_image(const std::string& path, std::string& error_message);
        [[nodiscard]] bool save_png(const std::string& path) const;
        void replace_rgba(
            std::span<const std::uint8_t> rgba,
            std::size_t width,
            std::size_t height);
        void upload_to_texture(GLuint texture_id);

        [[nodiscard]] std::size_t width() const noexcept { return m_width; }
        [[nodiscard]] std::size_t height() const noexcept { return m_height; }
        [[nodiscard]] std::span<std::uint8_t> rgba() noexcept;
        [[nodiscard]] std::span<const std::uint8_t> rgba() const noexcept;
        void mark_dirty() noexcept { m_dirty = true; }

    private:
        void set_pixel(std::size_t x, std::size_t y, RgbaColor color);

        std::size_t m_width {};
        std::size_t m_height {};
        std::vector<RgbaColor> m_pixels;
        bool m_dirty {true};
    };

    [[nodiscard]] GLuint create_rgba_texture(std::size_t width, std::size_t height);
    void upload_rgba_to_texture(
        GLuint texture_id,
        std::size_t width,
        std::size_t height,
        std::span<const std::uint8_t> rgba);
}
