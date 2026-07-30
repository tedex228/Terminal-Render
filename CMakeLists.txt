cmake_minimum_required(VERSION 3.16)
project(terminal-render VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(X11 REQUIRED)

add_executable(terminal-render
    src/main.cpp
    src/capture/capturer_x11.cpp
    src/processor/downscaler.cpp
    src/processor/ascii_conv.cpp
    src/output/term_renderer.cpp
)

target_include_directories(terminal-render PRIVATE src)
target_link_libraries(terminal-render PRIVATE X11::X11 X11::Xext)

install(TARGETS terminal-render DESTINATION bin)
