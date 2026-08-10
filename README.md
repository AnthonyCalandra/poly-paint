# poly-paint

Basic C++ GUI image app scaffolded with:

- CMake
- Dear ImGui
- GLFW
- GLAD
- stb_image_write

It opens a native desktop window, renders a pixel canvas, lets you paint on it, generate noise/gradients, and export PNG files.

## Build

This repo is configured for both clang and MSVC / Visual Studio.

### clang

```powershell
cmake --preset clang-debug
cmake --build --preset build-clang-debug
.\build\clang-debug\poly_paint.exe
```

### MSVC / Visual Studio 2022

```powershell
cmake --preset msvc-debug
cmake --build --preset build-msvc-debug
.\build\msvc-debug\Debug\poly_paint.exe
```

You can also open the folder directly in Visual Studio 2022 and select the `msvc-debug` or `msvc-release` preset.

The first configure step downloads dependencies from GitHub through CMake `FetchContent`, so network access is required for that step.

## Notes

- The clang presets use CMake's `Ninja` generator, which is included with Visual Studio's CMake tools. This avoids requiring a separate Unix `make` installation on Windows.
- The MSVC presets target Visual Studio 2022 (`Visual Studio 17 2022`) on `x64`.
- If `cmake --preset msvc-debug` fails, verify that Visual Studio 2022 with the Desktop development with C++ workload is installed.
- The app uses OpenGL 3.3 for rendering and Dear ImGui for the UI layer.
- Export currently writes PNG files to the project root by default.
