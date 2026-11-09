cmake_minimum_required(VERSION 3.25)

project(
    WindowsScrollCapture
    VERSION 0.1.0
    DESCRIPTION "Scrolling screenshot utility for Windows"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "Windows Scroll Capture only supports Windows.")
endif()

add_executable(Scroller WIN32
    src/main.cpp
)

target_compile_features(Scroller PRIVATE cxx_std_17)

target_include_directories(Scroller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(Scroller PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(Scroller PRIVATE
    user32
)

if(MSVC)
    target_compile_options(Scroller PRIVATE
        /W4
        /permissive-
        /EHsc
        /Zc:__cplusplus
    )
endif()

set_target_properties(Scroller PROPERTIES
    OUTPUT_NAME "WindowsScrollCapture"
)