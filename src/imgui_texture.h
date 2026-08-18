#pragma once

#include <glad/gl.h>
#include <imgui.h>

namespace poly_paint
{
    /** @brief Converts an OpenGL texture name to Dear ImGui's texture handle type. */
    [[nodiscard]] inline ImTextureID to_imgui_texture(GLuint texture) noexcept
    {
        return static_cast<ImTextureID>(texture);
    }
}
