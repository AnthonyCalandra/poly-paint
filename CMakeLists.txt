cmake_minimum_required(VERSION 3.24)

project(poly_paint
    VERSION 0.1.0
    DESCRIPTION "Basic C++ GUI image editor using Dear ImGui"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(POLY_PAINT_REPORT_SIMD_CAPABILITIES "Report CPU SIMD features during CMake configuration" OFF)
if(POLY_PAINT_REPORT_SIMD_CAPABILITIES)
    include(cmake/DetectSimdCapabilities.cmake)
    poly_paint_report_simd_capabilities()
endif()

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

FetchContent_MakeAvailable(glfw glad imgui stb bs_thread_pool)

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

add_executable(poly_paint
    src/main.cpp
)

target_include_directories(poly_paint
    PRIVATE
        ${stb_SOURCE_DIR}
)

target_link_libraries(poly_paint
    PRIVATE
        imgui
        glfw
        glad
        bs_thread_pool
)

if(WIN32)
    target_link_libraries(poly_paint PRIVATE opengl32 comdlg32)
endif()

if(MSVC)
    # This application targets the detected AVX2-capable development machine.
    target_compile_options(poly_paint PRIVATE /W4 /permissive- /arch:AVX2)
else()
    target_compile_options(poly_paint PRIVATE -Wall -Wextra -Wpedantic -mavx2)
endif()
