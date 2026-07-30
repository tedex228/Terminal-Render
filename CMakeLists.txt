cmake_minimum_required(VERSION 3.16)
project(terminal-render VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional X11
find_package(X11 QUIET)

# Optional PipeWire + systemd (for Wayland)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PW libpipewire-0.3)
endif()

add_executable(terminal-render
    src/main.cpp
    src/processor/downscaler.cpp
    src/processor/ascii_conv.cpp
    src/output/term_renderer.cpp
)

target_include_directories(terminal-render PRIVATE src)

if(X11_FOUND)
    target_sources(terminal-render PRIVATE src/capture/capturer_x11.cpp)
    target_link_libraries(terminal-render PRIVATE X11::X11 X11::Xext)
    target_compile_definitions(terminal-render PRIVATE HAS_X11)
endif()

if(PW_FOUND)
    target_sources(terminal-render PRIVATE src/capture/capturer_pw.cpp)
    target_include_directories(terminal-render PRIVATE ${PW_INCLUDE_DIRS})
    target_link_libraries(terminal-render PRIVATE ${PW_LIBRARIES} systemd gbm drm)
    target_compile_definitions(terminal-render PRIVATE HAS_PW)
endif()

install(TARGETS terminal-render DESTINATION bin)
