cmake_minimum_required(VERSION 3.24)

project(poly_paint
    VERSION 0.1.0
    DESCRIPTION "Basic C++ GUI image editor using Dear ImGui"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    # GLAD's CMake integration lives below the repository root.  Adding this
    # subdirectory makes glad_add_library available to this project.
    SOURCE_SUBDIR cmake
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.2b-docking
)

FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.8
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

FetchContent_Declare(
    bs_thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v5.1.0
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(
    highway
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.4.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)

set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glad imgui imgui_file_dialog stb bs_thread_pool highway)

add_library(bs_thread_pool INTERFACE)
target_include_directories(bs_thread_pool INTERFACE ${bs_thread_pool_SOURCE_DIR}/include)

glad_add_library(glad REPRODUCIBLE EXCLUDE_FROM_ALL LOADER API gl:core=3.3)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw glad)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

target_link_libraries(ImGuiFileDialog PUBLIC imgui)

add_executable(poly_paint
    src/highway_kernels.cpp
    src/application.cpp
    src/canvas.cpp
    src/canvas_panel.cpp
    src/cooperative_evolution.cpp
    src/controls_panel.cpp
    src/evolution_runner.cpp
    src/file_dialog.cpp
    src/gui_layout.cpp
    src/image_similarity_scorer.cpp
    src/main.cpp
    src/original_image_view.cpp
    src/polygon_evolution.cpp
    src/polygon_rasterizer.cpp
)

# Keep headers in a named file set so CMake-aware IDEs can discover them and
# the target metadata remains ready for future install/export rules.
target_sources(poly_paint
    PRIVATE
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS src
        FILES
            src/application.h
            src/canvas.h
            src/canvas_panel.h
            src/controls_panel.h
            src/cooperative_evolution.h
            src/evolution_runner.h
            src/evolution_strategy.h
            src/file_dialog.h
            src/gui_layout.h
            src/image_dimensions.h
            src/image_similarity_scorer.h
            src/imgui_texture.h
            src/original_image_view.h
            src/pixel_kernels.h
            src/polygon.h
            src/polygon_evolution.h
            src/polygon_rasterizer.h
)

target_include_directories(poly_paint
    PRIVATE
        src
        ${stb_SOURCE_DIR}
)

target_link_libraries(poly_paint
    PRIVATE
        ImGuiFileDialog
        imgui
        glfw
        glad
        hwy
        bs_thread_pool
)

if(WIN32)
    target_link_libraries(poly_paint PRIVATE opengl32)
endif()

if(MSVC)
    target_compile_options(poly_paint PRIVATE /W4 /permissive-)
else()
    target_compile_options(poly_paint PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(poly_paint_core_tests
        tests/core_tests.cpp
        src/highway_kernels.cpp
        src/cooperative_evolution.cpp
        src/evolution_runner.cpp
        src/image_similarity_scorer.cpp
        src/polygon_evolution.cpp
        src/polygon_rasterizer.cpp
    )
    target_sources(poly_paint_core_tests
        PRIVATE
            FILE_SET HEADERS
            TYPE HEADERS
            BASE_DIRS src
            FILES
                src/cooperative_evolution.h
                src/evolution_runner.h
                src/evolution_strategy.h
                src/image_dimensions.h
                src/image_similarity_scorer.h
                src/pixel_kernels.h
                src/polygon.h
                src/polygon_evolution.h
                src/polygon_rasterizer.h
    )
    target_include_directories(poly_paint_core_tests PRIVATE src)
    target_link_libraries(poly_paint_core_tests PRIVATE hwy bs_thread_pool)
    if(MSVC)
        target_compile_options(poly_paint_core_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(poly_paint_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME poly_paint_core_tests COMMAND poly_paint_core_tests)
endif()
