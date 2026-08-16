#include "application.h"

#include "canvas_panel.h"
#include "canvas.h"
#include "controls_panel.h"
#include "evolution_runner.h"
#include "file_dialog.h"
#include "gui_layout.h"
#include "image_similarity_scorer.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <string>

namespace poly_paint
{
    namespace
    {
        class Application
        {
        public:
            ~Application()
            {
                shutdown();
            }

            [[nodiscard]] int run()
            {
                if (!initialize())
                {
                    return 1;
                }

                while (!glfwWindowShouldClose(m_window))
                {
                    begin_frame();
                    consume_evolution_updates();

                    const PanelLayout layout = calculate_panel_layout();
                    draw_canvas_panel(
                        layout,
                        m_canvas,
                        m_canvas_texture,
                        m_controls.zoom(),
                        m_evolution.running());
                    const ControlsPanelActions actions = m_controls.draw(
                        layout,
                        ControlsPanelModel {
                            .current_generation = m_current_generation,
                            .best_score = m_best_score,
                            .original_image = {
                                m_original_image_texture,
                                m_target_scorer ? m_target_scorer->width() : 0,
                                m_target_scorer ? m_target_scorer->height() : 0
                            },
                            .has_target = m_target_scorer.has_value(),
                            .evolution_running = m_evolution.running(),
                            .evolution_pause_requested = m_evolution.pause_requested(),
                            .status_message = m_status_message
                        });
                    const std::optional selected_image_path = m_image_file_dialog.draw();
                    render_frame();
                    handle(actions);
                    if (selected_image_path)
                    {
                        load_image(*selected_image_path);
                    }
                }

                m_evolution.stop_and_wait();
                return 0;
            }

        private:
            [[nodiscard]] bool initialize()
            {
                if (!glfwInit())
                {
                    std::println(stderr, "Failed to initialize GLFW.");
                    return false;
                }
                m_glfw_initialized = true;

                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                m_window = glfwCreateWindow(1440, 900, "poly-paint", nullptr, nullptr);
                if (m_window == nullptr)
                {
                    std::println(stderr, "Failed to create GLFW window.");
                    return false;
                }

                glfwMakeContextCurrent(m_window);
                glfwSwapInterval(1);
                if (gladLoadGL(glfwGetProcAddress) == 0)
                {
                    std::println(stderr, "Failed to initialize GLAD.");
                    return false;
                }
                m_glad_initialized = true;

                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                m_imgui_context_created = true;
                ImGui::StyleColorsDark();
                if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true))
                {
                    std::println(stderr, "Failed to initialize the ImGui GLFW backend.");
                    return false;
                }
                m_imgui_glfw_initialized = true;
                if (!ImGui_ImplOpenGL3_Init("#version 330"))
                {
                    std::println(stderr, "Failed to initialize the ImGui OpenGL backend.");
                    return false;
                }
                m_imgui_opengl_initialized = true;

                m_canvas_texture = create_rgba_texture(m_canvas.width(), m_canvas.height());
                m_canvas.upload_to_texture(m_canvas_texture);
                return true;
            }

            void shutdown() noexcept
            {
                m_evolution.stop_and_wait();
                if (m_canvas_texture != 0 && m_glad_initialized)
                {
                    glDeleteTextures(1, &m_canvas_texture);
                    m_canvas_texture = 0;
                }
                if (m_original_image_texture != 0 && m_glad_initialized)
                {
                    glDeleteTextures(1, &m_original_image_texture);
                    m_original_image_texture = 0;
                }
                if (m_imgui_opengl_initialized)
                {
                    ImGui_ImplOpenGL3_Shutdown();
                    m_imgui_opengl_initialized = false;
                }
                if (m_imgui_glfw_initialized)
                {
                    ImGui_ImplGlfw_Shutdown();
                    m_imgui_glfw_initialized = false;
                }
                if (m_imgui_context_created)
                {
                    ImGui::DestroyContext();
                    m_imgui_context_created = false;
                }
                if (m_window != nullptr)
                {
                    glfwDestroyWindow(m_window);
                    m_window = nullptr;
                }
                if (m_glfw_initialized)
                {
                    glfwTerminate();
                    m_glfw_initialized = false;
                }
            }

            void begin_frame()
            {
                glfwPollEvents();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
            }

            void render_frame()
            {
                ImGui::Render();
                int display_width = 0;
                int display_height = 0;
                glfwGetFramebufferSize(m_window, &display_width, &display_height);
                glViewport(0, 0, display_width, display_height);
                glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(m_window);
            }

            void consume_evolution_updates()
            {
                if (std::optional update = m_evolution.take_latest_update())
                {
                    m_canvas.replace_rgba(
                        update->rgba, m_target_scorer->width(), m_target_scorer->height());
                    m_current_generation = update->generation;
                    m_best_score = update->score;
                }
                if (const std::optional stopped = m_evolution.join_if_finished())
                {
                    m_status_message = *stopped ? "Evolution stopped." : "Evolution finished.";
                }
            }

            void handle(const ControlsPanelActions& actions)
            {
                if (actions.open_image)
                {
                    m_image_file_dialog.open();
                }
                if (actions.resume_evolution)
                {
                    m_evolution.resume();
                    m_status_message = "Evolution resumed.";
                }
                if (actions.start_evolution)
                {
                    m_current_generation = 0;
                    m_best_score = 0.0;
                    m_evolution.start(
                        *m_target_scorer,
                        actions.start_evolution->maximum_generations,
                        actions.start_evolution->polygon_count,
                        actions.start_evolution->initial_population);
                    m_status_message = "Evolution running.";
                }
                if (actions.pause_evolution)
                {
                    m_evolution.request_pause();
                    m_status_message = "Pausing after this generation.";
                }
                if (actions.stop_evolution)
                {
                    m_evolution.request_stop();
                    m_status_message = "Stopping evolution after this generation.";
                }
                if (actions.save_path)
                {
                    m_status_message = m_canvas.save_png(*actions.save_path)
                        ? std::format("Saved {}", *actions.save_path)
                        : std::format("Failed to save {}", *actions.save_path);
                }
            }

            void load_image(const std::string& image_path)
            {
                std::string load_error;
                if (!m_canvas.load_image(image_path, load_error))
                {
                    m_status_message = std::format("Failed to load image: {}", load_error);
                    return;
                }

                glDeleteTextures(1, &m_canvas_texture);
                if (m_original_image_texture != 0)
                {
                    glDeleteTextures(1, &m_original_image_texture);
                }
                m_canvas_texture = create_rgba_texture(m_canvas.width(), m_canvas.height());
                m_target_scorer.emplace(m_canvas.width(), m_canvas.height(), m_canvas.rgba());
                m_original_image_texture =
                    create_rgba_texture(m_target_scorer->width(), m_target_scorer->height());
                upload_rgba_to_texture(
                    m_original_image_texture,
                    m_target_scorer->width(),
                    m_target_scorer->height(),
                    m_target_scorer->target_rgba());
                m_current_generation = 0;
                m_best_score = 0.0;
                m_status_message = std::format("Loaded {}", image_path);
            }

            GLFWwindow* m_window {};
            bool m_glfw_initialized {};
            bool m_glad_initialized {};
            bool m_imgui_context_created {};
            bool m_imgui_glfw_initialized {};
            bool m_imgui_opengl_initialized {};
            Canvas m_canvas {512, 512};
            GLuint m_canvas_texture {};
            GLuint m_original_image_texture {};
            ControlsPanel m_controls;
            ImageFileDialog m_image_file_dialog;
            std::optional<ImageSimilarityScorer> m_target_scorer;
            EvolutionRunner m_evolution;
            std::size_t m_current_generation {};
            double m_best_score {};
            std::string m_status_message {"Ready."};
        };
    }

    int run_application()
    {
        Application application;
        return application.run();
    }
}
