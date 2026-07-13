# Building StarWeb

The project builds on **macOS, Linux, and Windows**. There are three binaries:
`stwp_server`, `stwp_client`, and `stwp_browser`.

The media player in the browser uses a platform-specific backend, selected
automatically by the build system:

| Platform | Media backend | Source file |
|----------|---------------|-------------|
| macOS    | AVFoundation (native, hardware-accelerated) | `src/browser/media_player_mac.mm` |
| Linux    | FFmpeg decode + miniaudio output | `src/browser/media_player_ffmpeg.cpp` |
| Windows  | FFmpeg decode + miniaudio output | `src/browser/media_player_ffmpeg.cpp` |

`miniaudio` is vendored (`src/thirdparty/miniaudio.h`); FFmpeg is a system
dependency on Linux/Windows only.

---

## macOS

Dependencies: a compiler toolchain (Xcode command-line tools) and GLFW.

```sh
brew install glfw
make            # or: cmake -S . -B build && cmake --build build
```

## Linux

Dependencies: GLFW, OpenGL, and the FFmpeg development libraries.

```sh
# Debian / Ubuntu
sudo apt install build-essential pkg-config libglfw3-dev libgl1-mesa-dev \
     libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev

make            # or: cmake -S . -B build && cmake --build build
```

Audio output uses ALSA or PulseAudio, discovered at runtime by miniaudio — no
extra build-time audio dependency is required.

## Windows

Use CMake (the `Makefile` requires a POSIX shell). You need GLFW and FFmpeg
development libraries; [vcpkg](https://vcpkg.io) is the easiest way to get them:

```powershell
vcpkg install glfw3 ffmpeg
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Winsock (`ws2_32`) is linked automatically.

---

## Notes

- **Networking** is fully portable via `src/common/net.hpp`, a thin socket
  compatibility layer (POSIX sockets on macOS/Linux, Winsock2 on Windows). Use
  `net::socket_t` / `net::kInvalidSocket` rather than raw `int` / `-1`.
- To compile-check the FFmpeg backend on a Mac (which otherwise builds the
  AVFoundation path), define `STWP_FORCE_FFMPEG` and provide the FFmpeg headers.
