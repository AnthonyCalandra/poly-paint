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
    /** @brief Owns a mutable RGBA image and its OpenGL upload state. */
    class Canvas
    {
    public:
        /** @brief Creates a canvas with the specified pixel dimensions. */
        Canvas(std::size_t width, std::size_t height);

        /** @brief Fills every pixel with @p color. */
        void clear(RgbaColor color);
        /** @brief Fills the canvas with a generated color gradient. */
        void fill_gradient();
        /** @brief Fills the canvas with generated noise. */
        void fill_noise();
        /** @brief Loads an image from @p path, reporting any failure in @p error_message. */
        bool load_image(const std::string& path, std::string& error_message);
        /** @brief Writes the canvas to a PNG at @p path. */
        [[nodiscard]] bool save_png(const std::string& path) const;
        /** @brief Replaces the image with an RGBA buffer of the given dimensions. */
        void replace_rgba(
            std::span<const std::uint8_t> rgba,
            std::size_t width,
            std::size_t height);
        /** @brief Uploads pending pixel changes to @p texture_id. */
        void upload_to_texture(GLuint texture_id);

        /** @brief Returns the canvas width in pixels. */
        [[nodiscard]] std::size_t width() const noexcept { return m_width; }
        /** @brief Returns the canvas height in pixels. */
        [[nodiscard]] std::size_t height() const noexcept { return m_height; }
        /** @brief Returns mutable row-major RGBA bytes. */
        [[nodiscard]] std::span<std::uint8_t> rgba() noexcept;
        /** @brief Returns immutable row-major RGBA bytes. */
        [[nodiscard]] std::span<const std::uint8_t> rgba() const noexcept;
        /** @brief Marks the cached GPU texture as requiring an upload. */
        void mark_dirty() noexcept { m_dirty = true; }

    private:
        /** @brief Sets one pixel without changing the canvas dimensions. */
        void set_pixel(std::size_t x, std::size_t y, RgbaColor color);

        std::size_t m_width {};
        std::size_t m_height {};
        std::vector<RgbaColor> m_pixels;
        bool m_dirty {true};
    };

    /** @brief Creates an OpenGL texture sized for an RGBA image. */
    [[nodiscard]] GLuint create_rgba_texture(std::size_t width, std::size_t height);
    /** @brief Uploads row-major RGBA bytes to an existing OpenGL texture. */
    void upload_rgba_to_texture(
        GLuint texture_id,
        std::size_t width,
        std::size_t height,
        std::span<const std::uint8_t> rgba);
}
