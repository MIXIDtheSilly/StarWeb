# Cross-platform Makefile for macOS and Linux.
#
#   macOS : media backend = AVFoundation      (needs: glfw)
#   Linux : media backend = FFmpeg + miniaudio (needs: glfw, ffmpeg dev libs)
#
# On Windows, use CMake instead (this Makefile relies on a POSIX shell):
#   cmake -S . -B build && cmake --build build --config Release
# CMake also works on macOS/Linux if you prefer it over make.

UNAME_S := $(shell uname -s)

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -pthread

IMGUI_DIR = src/thirdparty/imgui
IMGUI_INC = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends

# Detect GLFW using pkg-config
GLFW_CFLAGS = $(shell pkg-config --cflags glfw3 2>/dev/null || echo "")
GLFW_LIBS = $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")

OBJ_DIR = obj

IMGUI_OBJS = $(OBJ_DIR)/imgui.o \
             $(OBJ_DIR)/imgui_draw.o \
             $(OBJ_DIR)/imgui_widgets.o \
             $(OBJ_DIR)/imgui_tables.o \
             $(OBJ_DIR)/imgui_impl_glfw.o \
             $(OBJ_DIR)/imgui_impl_opengl3.o

# ---- Platform-specific media backend and link flags ------------------------
ifeq ($(UNAME_S),Darwin)
    # macOS: AVFoundation-backed player, Apple frameworks, OpenGL framework.
    MEDIA_SRCS = src/browser/media_player_mac.mm
    MEDIA_FLAGS = -fobjc-arc
    MEDIA_LIBS = -framework AVFoundation -framework CoreMedia -framework AudioToolbox -framework QuartzCore
    GL_LIBS = -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
    # Linux: FFmpeg + miniaudio player.
    CXX = g++
    MEDIA_SRCS = src/browser/media_player_ffmpeg.cpp
    MEDIA_FLAGS =
    MEDIA_LIBS = $(shell pkg-config --libs libavcodec libavformat libavutil libswscale libswresample) -ldl -lm
    MEDIA_CFLAGS = $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale libswresample)
    GL_LIBS = -lGL
endif

TARGETS = stwp_server stwp_client stwp_browser

all: $(TARGETS)

# Rules to compile ImGui source files to object files
$(OBJ_DIR)/%.o: $(IMGUI_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GLFW_CFLAGS) $(IMGUI_INC) -c $< -o $@

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/backends/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GLFW_CFLAGS) $(IMGUI_INC) -c $< -o $@

stwp_server: src/server/server.cpp src/common/stwp_msg.hpp src/common/net.hpp
	$(CXX) $(CXXFLAGS) src/server/server.cpp -o stwp_server

stwp_client: src/client/client.cpp src/common/url_parser.hpp src/common/stwp_msg.hpp src/common/net.hpp
	$(CXX) $(CXXFLAGS) src/client/client.cpp -o stwp_client

stwp_browser: src/browser/browser.cpp src/browser/globals.cpp src/browser/parser.cpp src/browser/fetcher.cpp src/browser/renderer.cpp $(MEDIA_SRCS) $(IMGUI_OBJS) src/common/url_parser.hpp src/common/stwp_msg.hpp src/common/net.hpp
	$(CXX) $(CXXFLAGS) $(MEDIA_FLAGS) $(MEDIA_CFLAGS) $(GLFW_CFLAGS) $(IMGUI_INC) \
		src/browser/browser.cpp src/browser/globals.cpp src/browser/parser.cpp src/browser/fetcher.cpp src/browser/renderer.cpp $(MEDIA_SRCS) $(IMGUI_OBJS) \
		$(GLFW_LIBS) $(GL_LIBS) $(MEDIA_LIBS) -o stwp_browser

clean:
	rm -f stwp_server stwp_client stwp_browser
	rm -rf $(OBJ_DIR)

.PHONY: all clean
