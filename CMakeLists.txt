cmake_minimum_required(VERSION 3.16)
project(starweb LANGUAGES C CXX)

# Cross-platform build. Media backend: AVFoundation on macOS, FFmpeg+miniaudio
# elsewhere (see BUILDING.md for per-platform dependencies).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SRC ${CMAKE_SOURCE_DIR}/src)
set(IMGUI_DIR ${SRC}/thirdparty/imgui)

find_package(Threads REQUIRED)

# --- ImGui (core + GLFW/OpenGL3 backends) ----------------------------------
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

# --- GLFW + OpenGL ----------------------------------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# Prefer a CMake package (find_package(glfw3)); fall back to pkg-config.
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    set(GLFW_TARGET glfw)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    add_library(glfw_pc INTERFACE)
    target_include_directories(glfw_pc INTERFACE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(glfw_pc INTERFACE ${GLFW_LIBRARIES})
    target_link_directories(glfw_pc INTERFACE ${GLFW_LIBRARY_DIRS})
    set(GLFW_TARGET glfw_pc)
endif()
target_link_libraries(imgui PUBLIC ${GLFW_TARGET} OpenGL::GL)

# --- Networking libraries (Winsock on Windows) ------------------------------
set(NET_LIBS "")
if(WIN32)
    set(NET_LIBS ws2_32)
endif()

# --- Server & client --------------------------------------------------------
add_executable(stwp_server ${SRC}/server/server.cpp)
target_link_libraries(stwp_server PRIVATE Threads::Threads ${NET_LIBS})

add_executable(stwp_client ${SRC}/client/client.cpp)
target_link_libraries(stwp_client PRIVATE Threads::Threads ${NET_LIBS})

# --- Browser: pick the media backend for this platform ----------------------
set(BROWSER_SRCS
    ${SRC}/browser/browser.cpp
    ${SRC}/browser/globals.cpp
    ${SRC}/browser/parser.cpp
    ${SRC}/browser/fetcher.cpp
    ${SRC}/browser/renderer.cpp
)

add_executable(stwp_browser ${BROWSER_SRCS})
target_link_libraries(stwp_browser PRIVATE imgui Threads::Threads ${NET_LIBS})

if(APPLE)
    target_sources(stwp_browser PRIVATE ${SRC}/browser/media_player_mac.mm)
    set_source_files_properties(${SRC}/browser/media_player_mac.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(stwp_browser PRIVATE
        "-framework AVFoundation" "-framework CoreMedia"
        "-framework AudioToolbox" "-framework QuartzCore"
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
else()
    # FFmpeg + miniaudio backend for Windows / Linux.
    target_sources(stwp_browser PRIVATE ${SRC}/browser/media_player_ffmpeg.cpp)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec libavformat libavutil libswscale libswresample)
    target_link_libraries(stwp_browser PRIVATE PkgConfig::FFMPEG)

    if(UNIX)
        # miniaudio dlopen's the audio backend (ALSA/PulseAudio) at runtime.
        target_link_libraries(stwp_browser PRIVATE ${CMAKE_DL_LIBS} m)
    endif()
endif()
