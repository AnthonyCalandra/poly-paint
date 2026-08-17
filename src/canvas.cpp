#include "canvas.h"

#include "image_dimensions.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>

namespace poly_paint
{
    namespace
    {
        constexpr std::uint8_t maximum_channel = std::numeric_limits<std::uint8_t>::max();
        static_assert(sizeof(RgbaColor) == rgba_channel_count,
            "An RGBA color must contain four tightly packed channels.");
    }

    Canvas::Canvas(std::size_t width, std::size_t height)
        : m_width(width)
        , m_height(height)
    {
        if (width == 0 || height == 0)
        {
            throw std::invalid_argument("Canvas dimensions must be positive.");
        }
        m_pixels.resize(checked_pixel_count(width, height));
        clear({20, 20, 20, maximum_channel});
    }

    void Canvas::clear(RgbaColor color)
    {
        std::ranges::fill(m_pixels, color);
        m_dirty = true;
    }

    void Canvas::fill_gradient()
    {
        for (std::size_t y = 0; y < m_height; ++y)
        {
            for (std::size_t x = 0; x < m_width; ++x)
            {
                const float fx = static_cast<float>(x) /
                    static_cast<float>(std::max(std::size_t {1}, m_width - 1));
                const float fy = static_cast<float>(y) /
                    static_cast<float>(std::max(std::size_t {1}, m_height - 1));
                set_pixel(x, y, {
                    static_cast<std::uint8_t>(maximum_channel * fx),
                    static_cast<std::uint8_t>(maximum_channel * fy),
                    static_cast<std::uint8_t>(maximum_channel * (1.0f - fx)),
                    maximum_channel
                });
            }
        }
        m_dirty = true;
    }

    void Canvas::fill_noise()
    {
        std::mt19937 random_engine(static_cast<std::uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<unsigned int> channel(0, maximum_channel);
        for (RgbaColor& pixel : m_pixels)
        {
            pixel = {
                static_cast<std::uint8_t>(channel(random_engine)),
                static_cast<std::uint8_t>(channel(random_engine)),
                static_cast<std::uint8_t>(channel(random_engine)),
                maximum_channel
            };
        }
        m_dirty = true;
    }

    bool Canvas::load_image(const std::string& path, std::string& error_message)
    {
        int decoded_width = 0;
        int decoded_height = 0;
        int channels = 0;
        const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image_data {
            stbi_load(path.c_str(), &decoded_width, &decoded_height, &channels, STBI_rgb_alpha),
            &stbi_image_free
        };
        if (!image_data)
        {
            const char* reason = stbi_failure_reason();
            error_message = reason != nullptr ? reason : "unknown image decoding error";
            return false;
        }

        if (decoded_width <= 0 || decoded_height <= 0)
        {
            error_message = "decoded image dimensions are invalid";
            return false;
        }
        m_width = static_cast<std::size_t>(decoded_width);
        m_height = static_cast<std::size_t>(decoded_height);
        m_pixels.resize(checked_pixel_count(m_width, m_height));
        std::memcpy(m_pixels.data(), image_data.get(), checked_rgba_byte_count(m_width, m_height));
        m_dirty = true;
        error_message.clear();
        return true;
    }

    bool Canvas::save_png(const std::string& path) const
    {
        const int width = checked_narrow<int>(m_width, "PNG width");
        const int height = checked_narrow<int>(m_height, "PNG height");
        const int stride = checked_narrow<int>(
            checked_rgba_byte_count(m_width, 1), "PNG row stride");
        return stbi_write_png(
            path.c_str(),
            width,
            height,
            static_cast<int>(rgba_channel_count),
            m_pixels.data(),
            stride) != 0;
    }

    void Canvas::replace_rgba(
        std::span<const std::uint8_t> rgba,
        std::size_t width,
        std::size_t height)
    {
        if (width == 0 || height == 0 || rgba.size() != checked_rgba_byte_count(width, height))
        {
            throw std::invalid_argument("Canvas replacement buffer does not match its dimensions.");
        }
        m_width = width;
        m_height = height;
        m_pixels.resize(checked_pixel_count(width, height));
        std::memcpy(m_pixels.data(), rgba.data(), rgba.size());
        m_dirty = true;
    }

    void Canvas::upload_to_texture(GLuint texture_id)
    {
        if (!m_dirty)
        {
            return;
        }
        upload_rgba_to_texture(texture_id, m_width, m_height, rgba());
        m_dirty = false;
    }

    std::span<std::uint8_t> Canvas::rgba() noexcept
    {
        return {reinterpret_cast<std::uint8_t*>(m_pixels.data()), m_pixels.size() * sizeof(RgbaColor)};
    }

    std::span<const std::uint8_t> Canvas::rgba() const noexcept
    {
        return {reinterpret_cast<const std::uint8_t*>(m_pixels.data()), m_pixels.size() * sizeof(RgbaColor)};
    }

    void Canvas::set_pixel(std::size_t x, std::size_t y, RgbaColor color)
    {
        m_pixels[y * m_width + x] = color;
    }

    GLuint create_rgba_texture(std::size_t width, std::size_t height)
    {
        if (width == 0 || height == 0)
        {
            throw std::invalid_argument("Canvas texture dimensions must be positive.");
        }
        GLuint texture_id = 0;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8,
            checked_narrow<GLsizei>(width, "OpenGL texture width"),
            checked_narrow<GLsizei>(height, "OpenGL texture height"), 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        return texture_id;
    }

    void upload_rgba_to_texture(
        GLuint texture_id,
        std::size_t width,
        std::size_t height,
        std::span<const std::uint8_t> rgba)
    {
        if (texture_id == 0 || width == 0 || height == 0 ||
            rgba.size() != checked_rgba_byte_count(width, height))
        {
            throw std::invalid_argument("Texture upload requires a complete RGBA image.");
        }

        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0,
            checked_narrow<GLsizei>(width, "OpenGL texture width"),
            checked_narrow<GLsizei>(height, "OpenGL texture height"),
            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
}
