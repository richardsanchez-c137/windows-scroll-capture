
cmake_minimum_required(VERSION 3.25)

project(
    WindowsSnipTool
    VERSION 0.1.0
    DESCRIPTION "Native Windows screenshot and scrolling capture application"
    LANGUAGES CXX
)

# ============================================================
# Build requirements
# ============================================================

if(NOT WIN32)
    message(FATAL_ERROR "Windows Snip Tool requires Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows Snip Tool requires an x64 build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Application target
# ============================================================

add_executable(WindowsSnipTool WIN32
    src/main.cpp
    src/graphics_runtime.cpp
    src/window_renderer.cpp
    src/capture_runtime.cpp
    src/logger.cpp
)
target_include_directories(WindowsSnipTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(WindowsSnipTool PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

# ============================================================
# Windows SDK libraries
# ============================================================

target_link_libraries(WindowsSnipTool PRIVATE
    user32
    gdi32
    ole32
    d2d1
    dwrite
    windowscodecs
    d3d11
    dxgi
    windowsapp
)

# ============================================================
# Compiler configuration
# ============================================================

if(MSVC)
    target_compile_options(WindowsSnipTool PRIVATE
        /W4
        /permissive-
        /EHsc
        /Zc:__cplusplus
        /utf-8
    )

    target_compile_options(WindowsSnipTool PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
endif()

# ============================================================
# Output configuration
# ============================================================

set_target_properties(WindowsSnipTool PROPERTIES
    OUTPUT_NAME "WindowsSnipTool"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "==========================================")
message(STATUS " Windows Snip Tool")
message(STATUS " Version: ${PROJECT_VERSION}")
message(STATUS " C++ Standard: C++20")
message(STATUS " Architecture: x64")
message(STATUS "==========================================")
