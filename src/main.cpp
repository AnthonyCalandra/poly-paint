#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "evolution_strategy.h"
#include "image_similarity_scorer.h"
#include "polygon.h"
#include "polygon_rasterizer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <random>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
    struct Pixel
    {
        std::uint8_t r {};
        std::uint8_t g {};
        std::uint8_t b {};
        std::uint8_t a {255};
    };

    static_assert(sizeof(Pixel) == 4, "A pixel must contain four tightly packed color channels.");

    std::string open_image_file_dialog(GLFWwindow* window)
    {
#ifdef _WIN32
        char file_path[MAX_PATH] {};
        OPENFILENAMEA dialog {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = glfwGetWin32Window(window);
        dialog.lpstrFilter =
            "Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.hdr;*.pic\0"
            "All files\0*.*\0";
        dialog.lpstrFile = file_path;
        dialog.nMaxFile = sizeof(file_path);
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        return GetOpenFileNameA(&dialog) != FALSE ? file_path : std::string {};
#else
        (void)window;
        return {};
#endif
    }

    bool parse_generation_limit(
        const char* text,
        std::optional<std::size_t>& generation_limit,
        std::string& error_message)
    {
        if (*text == '\0')
        {
            generation_limit.reset();
            error_message.clear();
            return true;
        }

        unsigned long long parsed_value = 0;
        const char* end = text + std::strlen(text);
        const auto parse_result = std::from_chars(text, end, parsed_value);
        if (parse_result.ec != std::errc {} || parse_result.ptr != end ||
            parsed_value == 0 || parsed_value > std::numeric_limits<std::size_t>::max())
        {
            generation_limit.reset();
            error_message = "Enter a whole number greater than zero, or leave this blank.";
            return false;
        }

        generation_limit = static_cast<std::size_t>(parsed_value);
        error_message.clear();
        return true;
    }

    poly_paint::Polygon make_random_polygon(std::mt19937& random_engine)
    {
        std::uniform_real_distribution<float> coordinate(-0.15f, 1.15f);
        std::uniform_int_distribution<int> color_channel(0, 255);
        std::uniform_int_distribution<int> vertex_count(3, 7);

        const int count = vertex_count(random_engine);
        poly_paint::Polygon::VertexStorage vertices {};
        for (int index = 0; index < count; ++index)
        {
            vertices[static_cast<std::size_t>(index)] = {
                coordinate(random_engine),
                coordinate(random_engine)
            };
        }

        return {
            vertices,
            static_cast<std::size_t>(count),
            {
                static_cast<std::uint8_t>(color_channel(random_engine)),
                static_cast<std::uint8_t>(color_channel(random_engine)),
                static_cast<std::uint8_t>(color_channel(random_engine)),
                255
            }
        };
    }

    poly_paint::PolygonCollection make_random_polygon_collection(
        std::mt19937& random_engine,
        std::size_t polygon_count)
    {
        poly_paint::PolygonCollection collection(polygon_count);
        while (!collection.full())
        {
            collection.add(make_random_polygon(random_engine));
        }
        return collection;
    }

    struct ContrastSeed
    {
        int x {};
        int y {};
        poly_paint::RgbaColor color {};
        int contrast {};
    };

    std::vector<ContrastSeed> find_high_contrast_seeds(
        const std::uint8_t* rgba,
        int width,
        int height,
        std::size_t polygon_count)
    {
        const int sample_radius = std::clamp(std::min(width, height) / 64, 2, 16);
        const int sample_stride = std::max(1, sample_radius / 2);
        std::vector<ContrastSeed> candidates;
        candidates.reserve(static_cast<std::size_t>(width / sample_stride) *
            static_cast<std::size_t>(height / sample_stride));

        const auto color_at = [rgba, width](int x, int y, int channel)
        {
            return rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)];
        };
        for (int y = sample_radius; y < height - sample_radius; y += sample_stride)
        {
            for (int x = sample_radius; x < width - sample_radius; x += sample_stride)
            {
                int contrast = 0;
                for (int channel = 0; channel < 3; ++channel)
                {
                    const int surrounding_average =
                        (static_cast<int>(color_at(x - sample_radius, y, channel)) +
                         static_cast<int>(color_at(x + sample_radius, y, channel)) +
                         static_cast<int>(color_at(x, y - sample_radius, channel)) +
                         static_cast<int>(color_at(x, y + sample_radius, channel))) / 4;
                    contrast += std::abs(static_cast<int>(color_at(x, y, channel)) - surrounding_average);
                }
                candidates.push_back({
                    x,
                    y,
                    {color_at(x, y, 0), color_at(x, y, 1), color_at(x, y, 2), 255},
                    contrast
                });
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const ContrastSeed& left, const ContrastSeed& right)
        {
            return left.contrast > right.contrast;
        });

        std::vector<ContrastSeed> seeds;
        seeds.reserve(polygon_count);
        const float minimum_separation = 0.55f * std::sqrt(
            static_cast<float>(width) * static_cast<float>(height) /
            static_cast<float>(polygon_count));
        const float minimum_separation_squared = minimum_separation * minimum_separation;
        for (const ContrastSeed& candidate : candidates)
        {
            const bool sufficiently_separated = std::all_of(
                seeds.begin(), seeds.end(), [&candidate, minimum_separation_squared](const ContrastSeed& selected)
                {
                    const float delta_x = static_cast<float>(candidate.x - selected.x);
                    const float delta_y = static_cast<float>(candidate.y - selected.y);
                    return delta_x * delta_x + delta_y * delta_y >= minimum_separation_squared;
                });
            if (sufficiently_separated)
            {
                seeds.push_back(candidate);
                if (seeds.size() == polygon_count)
                {
                    break;
                }
            }
        }

        // Small or very uniform images can have fewer well-separated candidates.
        // Retain the strongest remaining locations so the genome still has 50 polygons.
        for (const ContrastSeed& candidate : candidates)
        {
            if (seeds.size() == polygon_count)
            {
                break;
            }
            const bool already_selected = std::any_of(seeds.begin(), seeds.end(), [&candidate](const ContrastSeed& selected)
            {
                return selected.x == candidate.x && selected.y == candidate.y;
            });
            if (!already_selected)
            {
                seeds.push_back(candidate);
            }
        }
        return seeds;
    }

    poly_paint::PolygonCollection make_best_guess_polygon_collection(
        const std::vector<ContrastSeed>& seeds,
        int width,
        int height,
        std::size_t polygon_count,
        std::mt19937& random_engine)
    {
        poly_paint::PolygonCollection collection(polygon_count);
        const std::size_t initial_seed_count = seeds.size();
        for (std::size_t index = 0; index < initial_seed_count; ++index)
        {
            const ContrastSeed& seed = seeds[index];
            const float area_per_polygon = static_cast<float>(width) * static_cast<float>(height) /
                static_cast<float>(polygon_count);
            const float base_radius = std::sqrt(area_per_polygon / 2.6f);
            std::normal_distribution<float> position_jitter(0.0f, base_radius * 0.10f);
            std::normal_distribution<float> scale_jitter(1.0f, 0.12f);
            const float center_x = static_cast<float>(seed.x) + position_jitter(random_engine);
            const float center_y = static_cast<float>(seed.y) + position_jitter(random_engine);
            const float radius = base_radius * std::clamp(scale_jitter(random_engine), 0.65f, 1.35f);
            poly_paint::Polygon::VertexStorage vertices {};
            constexpr std::size_t vertex_count = 6;
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                constexpr float pi = 3.14159265358979323846f;
                const float angle = (2.0f * pi * static_cast<float>(vertex_index)) /
                    static_cast<float>(vertex_count);
                vertices[vertex_index] = {
                    (center_x + std::cos(angle) * radius) / static_cast<float>(width),
                    (center_y + std::sin(angle) * radius) / static_cast<float>(height)
                };
            }
            collection.add({
                vertices,
                vertex_count,
                seed.color
            });
        }
        return collection;
    }

    poly_paint::PolygonCollection mutate_polygon_collection(
        const poly_paint::PolygonCollection& parent,
        std::mt19937& random_engine)
    {
        poly_paint::PolygonCollection child = parent;
        std::uniform_int_distribution<std::size_t> polygon_index(0, child.size() - 1);
        const std::size_t selected_polygon = polygon_index(random_engine);
        std::uniform_int_distribution<int> mutation_tier(0, 99);
        const int tier = mutation_tier(random_engine);
        if (tier >= 95)
        {
            // 5%: introduce a completely new shape and color.
            child.replace(selected_polygon, make_random_polygon(random_engine));
            return child;
        }

        poly_paint::Polygon updated_polygon = child.polygon_at(selected_polygon);
        std::uniform_int_distribution<int> teleport_roll(0, 99);
        if (teleport_roll(random_engine) < 10)
        {
            // Preserve the complete polygon and its color, but give it a real
            // opportunity to discover a distant area of the image.
            poly_paint::PolygonPoint centroid {};
            for (std::size_t index = 0; index < updated_polygon.vertex_count(); ++index)
            {
                const poly_paint::PolygonPoint& vertex = updated_polygon.vertex_at(index);
                centroid.x += vertex.x;
                centroid.y += vertex.y;
            }
            const float inverse_vertex_count = 1.0f / static_cast<float>(updated_polygon.vertex_count());
            centroid.x *= inverse_vertex_count;
            centroid.y *= inverse_vertex_count;

            std::uniform_real_distribution<float> canvas_position(0.0f, 1.0f);
            const float offset_x = canvas_position(random_engine) - centroid.x;
            const float offset_y = canvas_position(random_engine) - centroid.y;
            for (std::size_t index = 0; index < updated_polygon.vertex_count(); ++index)
            {
                poly_paint::PolygonPoint vertex = updated_polygon.vertex_at(index);
                // Do not clamp here: a translation must not distort the shape.
                // The rasterizer clips any portion that extends past the canvas.
                vertex.x += offset_x;
                vertex.y += offset_y;
                updated_polygon.set_vertex(index, vertex);
            }
            child.replace(selected_polygon, std::move(updated_polygon));
            return child;
        }

        // Keep color and single-vertex edits, while also allowing large shapes
        // to move or resize as a unit.
        std::uniform_int_distribution<int> mutation_type(0, 3);
        const bool medium_mutation = tier >= 70; // 25% medium, 70% small.

        switch (mutation_type(random_engine))
        {
        case 0:
        {
            poly_paint::RgbaColor color = updated_polygon.color();
            std::uniform_int_distribution<int> channel(0, 2);
            std::normal_distribution<float> color_change(0.0f, medium_mutation ? 64.0f : 16.0f);
            std::uint8_t* channels[] {&color.r, &color.g, &color.b};
            const std::size_t selected_channel = static_cast<std::size_t>(channel(random_engine));
            const int changed = std::clamp(
                static_cast<int>(*channels[selected_channel] + color_change(random_engine)),
                0,
                255);
            *channels[selected_channel] = static_cast<std::uint8_t>(changed);
            updated_polygon.set_color(color);
            break;
        }
        case 1:
        {
            std::uniform_int_distribution<std::size_t> vertex_index(0, updated_polygon.vertex_count() - 1);
            std::normal_distribution<float> position_change(0.0f, medium_mutation ? 0.16f : 0.04f);
            const std::size_t selected_vertex = vertex_index(random_engine);
            poly_paint::PolygonPoint vertex = updated_polygon.vertex_at(selected_vertex);
            vertex.x = std::clamp(vertex.x + position_change(random_engine), -0.25f, 1.25f);
            vertex.y = std::clamp(vertex.y + position_change(random_engine), -0.25f, 1.25f);
            updated_polygon.set_vertex(selected_vertex, vertex);
            break;
        }
        case 2:
        {
            // Translate every vertex by the same amount, preserving shape and size.
            std::normal_distribution<float> translation(0.0f, medium_mutation ? 0.16f : 0.04f);
            const float offset_x = translation(random_engine);
            const float offset_y = translation(random_engine);
            for (std::size_t index = 0; index < updated_polygon.vertex_count(); ++index)
            {
                poly_paint::PolygonPoint vertex = updated_polygon.vertex_at(index);
                vertex.x = std::clamp(vertex.x + offset_x, -0.25f, 1.25f);
                vertex.y = std::clamp(vertex.y + offset_y, -0.25f, 1.25f);
                updated_polygon.set_vertex(index, vertex);
            }
            break;
        }
        default:
        {
            // Scale around the vertex centroid, preserving the polygon's position.
            poly_paint::PolygonPoint centroid {};
            for (std::size_t index = 0; index < updated_polygon.vertex_count(); ++index)
            {
                const poly_paint::PolygonPoint& vertex = updated_polygon.vertex_at(index);
                centroid.x += vertex.x;
                centroid.y += vertex.y;
            }
            const float inverse_vertex_count = 1.0f / static_cast<float>(updated_polygon.vertex_count());
            centroid.x *= inverse_vertex_count;
            centroid.y *= inverse_vertex_count;

            std::normal_distribution<float> scale_change(1.0f, medium_mutation ? 0.50f : 0.15f);
            const float scale = std::clamp(scale_change(random_engine), 0.25f, 2.5f);
            for (std::size_t index = 0; index < updated_polygon.vertex_count(); ++index)
            {
                poly_paint::PolygonPoint vertex = updated_polygon.vertex_at(index);
                vertex.x = std::clamp(centroid.x + (vertex.x - centroid.x) * scale, -0.25f, 1.25f);
                vertex.y = std::clamp(centroid.y + (vertex.y - centroid.y) * scale, -0.25f, 1.25f);
                updated_polygon.set_vertex(index, vertex);
            }
            break;
        }
        }

        child.replace(selected_polygon, std::move(updated_polygon));
        return child;
    }

    class Canvas
    {
    public:
        Canvas(int width, int height)
            : m_width(width), m_height(height), m_pixels(static_cast<std::size_t>(width * height))
        {
            clear({20, 20, 20, 255});
        }

        void clear(Pixel color)
        {
            std::fill(m_pixels.begin(), m_pixels.end(), color);
            m_dirty = true;
        }

        void fill_gradient()
        {
            for (int y = 0; y < m_height; ++y)
            {
                for (int x = 0; x < m_width; ++x)
                {
                    const float fx = static_cast<float>(x) / static_cast<float>(m_width - 1);
                    const float fy = static_cast<float>(y) / static_cast<float>(m_height - 1);
                    set_pixel(x, y, {
                        static_cast<std::uint8_t>(255.0f * fx),
                        static_cast<std::uint8_t>(255.0f * fy),
                        static_cast<std::uint8_t>(255.0f * (1.0f - fx)),
                        255
                    });
                }
            }
            m_dirty = true;
        }

        void fill_noise()
        {
            std::mt19937 rng(static_cast<std::uint32_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            std::uniform_int_distribution<int> dist(0, 255);

            for (auto& pixel : m_pixels)
            {
                pixel = {
                    static_cast<std::uint8_t>(dist(rng)),
                    static_cast<std::uint8_t>(dist(rng)),
                    static_cast<std::uint8_t>(dist(rng)),
                    255
                };
            }

            m_dirty = true;
        }

        bool load_image(const std::string& path, std::string& error_message)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* image_data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (image_data == nullptr)
            {
                const char* reason = stbi_failure_reason();
                error_message = reason != nullptr ? reason : "unknown image decoding error";
                return false;
            }

            m_width = width;
            m_height = height;
            m_pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            std::memcpy(m_pixels.data(), image_data, m_pixels.size() * sizeof(Pixel));
            stbi_image_free(image_data);
            m_dirty = true;
            return true;
        }

        bool save_png(const std::string& path) const
        {
            return stbi_write_png(
                       path.c_str(),
                       m_width,
                       m_height,
                       4,
                       m_pixels.data(),
                       m_width * static_cast<int>(sizeof(Pixel))) != 0;
        }

        void upload_to_texture(GLuint texture_id)
        {
            if (!m_dirty)
            {
                return;
            }

            glBindTexture(GL_TEXTURE_2D, texture_id);
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                m_width,
                m_height,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                m_pixels.data());

            m_dirty = false;
        }

        void replace_rgba(const std::vector<std::uint8_t>& rgba, int width, int height)
        {
            const std::size_t expected_size =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(Pixel);
            if (width <= 0 || height <= 0 || rgba.size() != expected_size)
            {
                throw std::invalid_argument("Canvas replacement buffer does not match its dimensions.");
            }

            m_width = width;
            m_height = height;
            m_pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            std::memcpy(m_pixels.data(), rgba.data(), rgba.size());
            m_dirty = true;
        }

        [[nodiscard]] int width() const { return m_width; }
        [[nodiscard]] int height() const { return m_height; }

        // The canvas uses four bytes per pixel in RGBA order. Call mark_dirty()
        // after writing through this pointer so the next frame uploads it to OpenGL.
        [[nodiscard]] std::uint8_t* rgba_data()
        {
            return reinterpret_cast<std::uint8_t*>(m_pixels.data());
        }

        [[nodiscard]] const std::uint8_t* rgba_data() const
        {
            return reinterpret_cast<const std::uint8_t*>(m_pixels.data());
        }

        [[nodiscard]] std::size_t rgba_size_bytes() const
        {
            return m_pixels.size() * sizeof(Pixel);
        }

        void mark_dirty()
        {
            m_dirty = true;
        }

    private:
        void set_pixel(int x, int y, Pixel color)
        {
            m_pixels[static_cast<std::size_t>(y * m_width + x)] = color;
        }

        int m_width {};
        int m_height {};
        std::vector<Pixel> m_pixels {};
        bool m_dirty {true};
    };

    GLuint create_canvas_texture(int width, int height)
    {
        GLuint texture_id = 0;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        return texture_id;
    }
}

int main()
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "Failed to initialize GLFW.\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "poly-paint", nullptr, nullptr);
    if (window == nullptr)
    {
        std::fprintf(stderr, "Failed to create GLFW window.\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (gladLoadGL(glfwGetProcAddress) == 0)
    {
        std::fprintf(stderr, "Failed to initialize GLAD.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Canvas canvas(512, 512);
    GLuint canvas_texture = create_canvas_texture(canvas.width(), canvas.height());
    canvas.upload_to_texture(canvas_texture);

    float zoom = 1.0f;
    char generation_limit_buffer[32] {};
    std::optional<std::size_t> generation_limit;
    std::string generation_limit_error;
    std::size_t current_generation = 0;
    int initial_population_mode = 0; // 0: randomized, 1: contrast-based best guess.
    int polygon_count_preset = 0;
    constexpr std::array<std::size_t, 4> polygon_count_presets {50, 100, 500, 1'000};
    bool has_opened_image = false;
    std::optional<poly_paint::ImageSimilarityScorer> target_scorer;
    std::thread evolution_thread;
    std::atomic<bool> evolution_running {false};
    std::atomic<bool> evolution_cancel_requested {false};
    std::atomic<bool> evolution_pause_requested {false};
    std::mutex evolution_pause_mutex;
    std::condition_variable evolution_pause_condition;
    std::mutex evolution_result_mutex;
    std::vector<std::uint8_t> latest_candidate_rgba;
    std::size_t latest_candidate_generation = 0;
    double latest_candidate_score = 0.0;
    bool has_pending_candidate = false;
    double best_score = 0.0;
    std::string export_path = "output.png";
    std::string status_message = "Ready.";

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            std::lock_guard<std::mutex> lock(evolution_result_mutex);
            if (has_pending_candidate)
            {
                canvas.replace_rgba(latest_candidate_rgba, target_scorer->width(), target_scorer->height());
                current_generation = latest_candidate_generation;
                best_score = latest_candidate_score;
                has_pending_candidate = false;
            }
        }
        if (!evolution_running && evolution_thread.joinable())
        {
            evolution_thread.join();
            status_message = evolution_cancel_requested
                ? "Evolution stopped."
                : "Evolution finished.";
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 work_pos = viewport->WorkPos;
        const ImVec2 work_size = viewport->WorkSize;
        const float controls_width = std::clamp(work_size.x * 0.28f, 280.0f, 380.0f);
        const float canvas_width = std::max(1.0f, work_size.x - controls_width);
        constexpr ImGuiWindowFlags fixed_panel_flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos(work_pos);
        ImGui::SetNextWindowSize(ImVec2(canvas_width, work_size.y));
        ImGui::Begin("Canvas", nullptr, fixed_panel_flags);

        canvas.upload_to_texture(canvas_texture);
        const ImVec2 image_size {
            static_cast<float>(canvas.width()) * zoom,
            static_cast<float>(canvas.height()) * zoom
        };
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > image_size.x)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - image_size.x) * 0.5f);
        }
        if (available.y > image_size.y)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (available.y - image_size.y) * 0.5f);
        }
        ImGui::Image(
            static_cast<ImTextureID>(static_cast<std::intptr_t>(canvas_texture)),
            image_size);
        if (evolution_running)
        {
            char fps_text[32] {};
            std::snprintf(fps_text, sizeof(fps_text), "%.0f FPS", ImGui::GetIO().Framerate);

            const ImVec2 image_min = ImGui::GetItemRectMin();
            const ImVec2 image_max = ImGui::GetItemRectMax();
            const ImVec2 text_size = ImGui::CalcTextSize(fps_text);
            const ImVec2 text_position {
                image_max.x - text_size.x - 12.0f,
                image_min.y + 10.0f
            };
            ImDrawList* overlay = ImGui::GetWindowDrawList();
            overlay->AddRectFilled(
                ImVec2(text_position.x - 6.0f, text_position.y - 4.0f),
                ImVec2(text_position.x + text_size.x + 6.0f, text_position.y + text_size.y + 4.0f),
                IM_COL32(0, 0, 0, 180),
                4.0f);
            overlay->AddText(text_position, IM_COL32(255, 255, 255, 255), fps_text);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(work_pos.x + canvas_width, work_pos.y));
        ImGui::SetNextWindowSize(ImVec2(controls_width, work_size.y));
        ImGui::Begin("Controls", nullptr, fixed_panel_flags);

        if (evolution_running)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Open image..."))
        {
            const std::string image_path = open_image_file_dialog(window);
            if (!image_path.empty())
            {
                std::string load_error;
                if (canvas.load_image(image_path, load_error))
                {
                    glDeleteTextures(1, &canvas_texture);
                    canvas_texture = create_canvas_texture(canvas.width(), canvas.height());
                    has_opened_image = true;
                    target_scorer.emplace(canvas.width(), canvas.height(), canvas.rgba_data());
                    current_generation = 0;
                    best_score = 0.0;
                    status_message = "Loaded " + image_path;
                }
                else
                {
                    status_message = "Failed to load image: " + load_error;
                }
            }
        }
        if (evolution_running)
        {
            ImGui::EndDisabled();
        }
        ImGui::SliderFloat("Zoom", &zoom, 0.5f, 6.0f, "%.2fx");
        ImGui::Separator();

        ImGui::Text("Current generation: %zu", current_generation);
        ImGui::InputText(
            "Generation limit",
            generation_limit_buffer,
            sizeof(generation_limit_buffer),
            ImGuiInputTextFlags_CharsDecimal);
        parse_generation_limit(generation_limit_buffer, generation_limit, generation_limit_error);
        if (!generation_limit_error.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", generation_limit_error.c_str());
        }
        else if (generation_limit)
        {
            ImGui::Text("Run will stop after %zu generations.", *generation_limit);
        }
        else
        {
            ImGui::TextUnformatted("No generation limit.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Initial population");
        if (evolution_running)
        {
            ImGui::BeginDisabled();
        }
        ImGui::RadioButton("Randomized polygons", &initial_population_mode, 0);
        ImGui::RadioButton("Best guess", &initial_population_mode, 1);
        char selected_polygon_count[16] {};
        std::snprintf(
            selected_polygon_count,
            sizeof(selected_polygon_count),
            "%zu",
            polygon_count_presets[static_cast<std::size_t>(polygon_count_preset)]);
        ImGui::SliderInt(
            "Polygons",
            &polygon_count_preset,
            0,
            static_cast<int>(polygon_count_presets.size() - 1),
            selected_polygon_count,
            ImGuiSliderFlags_NoInput);
        const ImVec2 polygon_slider_min = ImGui::GetItemRectMin();
        const ImVec2 polygon_slider_max = ImGui::GetItemRectMax();
        ImDrawList* polygon_slider_draw_list = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index < polygon_count_presets.size(); ++index)
        {
            char preset_label[16] {};
            std::snprintf(preset_label, sizeof(preset_label), "%zu", polygon_count_presets[index]);
            const ImVec2 label_size = ImGui::CalcTextSize(preset_label);
            const float fraction = static_cast<float>(index) /
                static_cast<float>(polygon_count_presets.size() - 1);
            const float center_x = polygon_slider_min.x +
                (polygon_slider_max.x - polygon_slider_min.x) * fraction;
            polygon_slider_draw_list->AddText(
                ImVec2(center_x - label_size.x * 0.5f, polygon_slider_max.y + 3.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                preset_label);
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() + 3.0f));
        if (evolution_running)
        {
            ImGui::EndDisabled();
        }

        const bool can_start_evolution = has_opened_image && target_scorer.has_value() &&
            (!evolution_running || evolution_pause_requested);
        if (!can_start_evolution)
        {
            ImGui::BeginDisabled();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.21f, 0.68f, 0.37f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.42f, 0.22f, 1.0f));
        if (ImGui::Button(evolution_pause_requested ? "Resume Evolution" : "Start Evolution", ImVec2(-1.0f, 0.0f)))
        {
            if (evolution_running)
            {
                evolution_pause_requested = false;
                evolution_pause_condition.notify_all();
                status_message = "Evolution resumed.";
            }
            else
            {
                if (evolution_thread.joinable())
                {
                    evolution_thread.join();
                }

                const poly_paint::ImageSimilarityScorer target = *target_scorer;
                const std::size_t maximum_generations = generation_limit.value_or(0);
                const std::size_t polygon_count =
                    polygon_count_presets[static_cast<std::size_t>(polygon_count_preset)];
                const bool use_best_guess = initial_population_mode == 1;
                std::vector<ContrastSeed> best_guess_seeds;
                if (use_best_guess)
                {
                    best_guess_seeds = find_high_contrast_seeds(
                        target.target_rgba(), target.width(), target.height(), polygon_count);
                }
                current_generation = 0;
                best_score = 0.0;
                evolution_cancel_requested = false;
                evolution_pause_requested = false;
                evolution_running = true;
                status_message = "Evolution running.";
                evolution_thread = std::thread([
                target,
                maximum_generations,
                polygon_count,
                use_best_guess,
                best_guess_seeds = std::move(best_guess_seeds),
                &evolution_cancel_requested,
                &evolution_pause_requested,
                &evolution_pause_mutex,
                &evolution_pause_condition,
                &evolution_result_mutex,
                &latest_candidate_rgba,
                &latest_candidate_generation,
                &latest_candidate_score,
                &has_pending_candidate,
                &evolution_running]()
            {
                using Strategy = poly_paint::EvolutionStrategy<poly_paint::PolygonCollection>;
                Strategy::Settings settings;
                settings.parent_count = 5;
                settings.offspring_count = 20;
                settings.max_generations = maximum_generations;

                const std::size_t image_byte_count =
                    static_cast<std::size_t>(target.width()) *
                    static_cast<std::size_t>(target.height()) * 4;
                std::vector<std::uint8_t> display_image(image_byte_count);

                Strategy strategy(
                    settings,
                    [use_best_guess,
                     best_guess_seeds = std::move(best_guess_seeds),
                     width = target.width(),
                     height = target.height(),
                     polygon_count](std::mt19937& random_engine)
                    {
                        if (use_best_guess)
                        {
                            return make_best_guess_polygon_collection(
                                best_guess_seeds, width, height, polygon_count, random_engine);
                        }
                        return make_random_polygon_collection(random_engine, polygon_count);
                    },
                    mutate_polygon_collection,
                    [&target](const poly_paint::PolygonCollection& candidate)
                    {
                        constexpr int tile_row_count = 64;
                        thread_local std::vector<std::uint8_t> tile_rgba;
                        const std::size_t required_size =
                            static_cast<std::size_t>(target.width()) * tile_row_count * 4;
                        tile_rgba.resize(required_size);

                        std::uint64_t total_difference = 0;
                        for (int first_row = 0; first_row < target.height(); first_row += tile_row_count)
                        {
                            const int rows_in_tile = std::min(tile_row_count, target.height() - first_row);
                            poly_paint::PolygonRasterizer::rasterize_rows_into(
                                candidate,
                                target.width(),
                                target.height(),
                                first_row,
                                rows_in_tile,
                                tile_rgba.data());
                            total_difference += target.difference_for_rows(
                                tile_rgba.data(), first_row, rows_in_tile);
                        }
                        return target.score_from_total_difference(total_difference);
                    },
                    {},
                    [&target,
                     &evolution_cancel_requested,
                     &evolution_pause_requested,
                     &evolution_pause_mutex,
                     &evolution_pause_condition,
                     &evolution_result_mutex,
                     &latest_candidate_rgba,
                     &latest_candidate_generation,
                     &latest_candidate_score,
                     &has_pending_candidate,
                     &display_image](
                        std::size_t generation,
                        const poly_paint::PolygonCollection& best_candidate,
                        const double& score)
                    {
                        poly_paint::PolygonRasterizer::rasterize_into(
                            best_candidate,
                            target.width(),
                            target.height(),
                            display_image.data());
                        {
                            std::lock_guard<std::mutex> lock(evolution_result_mutex);
                            latest_candidate_rgba = display_image;
                            latest_candidate_generation = generation;
                            latest_candidate_score = score;
                            has_pending_candidate = true;
                        }
                        if (evolution_pause_requested)
                        {
                            std::unique_lock<std::mutex> lock(evolution_pause_mutex);
                            evolution_pause_condition.wait(lock, [&evolution_pause_requested, &evolution_cancel_requested]
                            {
                                return !evolution_pause_requested.load() || evolution_cancel_requested.load();
                            });
                        }
                        return !evolution_cancel_requested.load();
                });

                std::mt19937 random_engine(std::random_device {}());
                [[maybe_unused]] const Strategy::Result result = strategy.run(random_engine);
                evolution_running = false;
            });
            }
        }
        ImGui::PopStyleColor(3);

        if (!can_start_evolution)
        {
            ImGui::EndDisabled();
            if (!has_opened_image)
            {
                ImGui::TextDisabled("Open an image to enable evolution.");
            }
        }

        if (evolution_running)
        {
            if (!evolution_pause_requested)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.48f, 0.12f, 1.0f));
                if (ImGui::Button("Pause Evolution", ImVec2(-1.0f, 0.0f)))
                {
                    evolution_pause_requested = true;
                    status_message = "Pausing after this generation.";
                }
                ImGui::PopStyleColor();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
            if (ImGui::Button("Stop Evolution", ImVec2(-1.0f, 0.0f)))
            {
                evolution_cancel_requested = true;
                evolution_pause_requested = false;
                evolution_pause_condition.notify_all();
                status_message = "Stopping evolution after this generation.";
            }
            ImGui::PopStyleColor();
            ImGui::Text("Best similarity: %.4f", best_score);
        }

        static char export_buffer[260] = "output.png";
        ImGui::InputText("Export path", export_buffer, sizeof(export_buffer));
        if (ImGui::Button("Save PNG"))
        {
            export_path = export_buffer;
            status_message = canvas.save_png(export_path)
                ? "Saved " + export_path
                : "Failed to save " + export_path;
        }

        ImGui::Separator();
        ImGui::TextWrapped("%s", status_message.c_str());
        ImGui::End();

        ImGui::Render();

        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    evolution_cancel_requested = true;
    evolution_pause_requested = false;
    evolution_pause_condition.notify_all();
    if (evolution_thread.joinable())
    {
        evolution_thread.join();
    }
    glDeleteTextures(1, &canvas_texture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
