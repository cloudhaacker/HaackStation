cmake_minimum_required(VERSION 3.20)
project(HaackStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Options ───────────────────────────────────────────────────────────────────
option(HAACK_BUILD_ANDROID   "Build for Android (set by NDK toolchain)" OFF)
option(HAACK_ENABLE_VULKAN   "Enable Vulkan renderer backend"           ON)
option(HAACK_ENABLE_OPENGL   "Enable OpenGL renderer backend"           ON)
option(HAACK_ENABLE_NCNN     "Enable NCNN AI upscaling"                 OFF)
option(HAACK_ENABLE_NETPLAY  "Enable netplay (GGPO rollback)"           ON)

# ─── Dependencies ──────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)

if(HAACK_ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        add_compile_definitions(HAACK_VULKAN)
    endif()
endif()

if(HAACK_ENABLE_OPENGL AND NOT HAACK_BUILD_ANDROID)
    find_package(OpenGL)
endif()

# ─── Beetle PSX Core (libretro) ────────────────────────────────────────────────
# The emulation core — Beetle PSX HW is fetched as a submodule
# Provides cycle-accurate MIPS, GPU, SPU, CDROM emulation
if(EXISTS "${CMAKE_SOURCE_DIR}/core/beetle-psx-libretro/Makefile")
    message(STATUS "Beetle PSX HW core found")
    set(BEETLE_CORE_FOUND TRUE)
else()
    message(WARNING
        "Beetle PSX HW core not found.\n"
        "Run: git submodule update --init --recursive\n"
        "Or:  git clone https://github.com/libretro/beetle-psx-libretro core/beetle-psx-libretro")
    set(BEETLE_CORE_FOUND FALSE)
endif()

# ─── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(frontend)
add_subdirectory(audio)
add_subdirectory(textures)
add_subdirectory(shaders)

if(HAACK_ENABLE_NETPLAY)
    add_subdirectory(netplay)
endif()

# ─── Version header ────────────────────────────────────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/frontend/include/version.h.in"
    "${CMAKE_BINARY_DIR}/include/version.h"
)

message(STATUS "────────────────────────────────────────")
message(STATUS "  HaackStation ${PROJECT_VERSION}")
message(STATUS "  Vulkan:  ${HAACK_ENABLE_VULKAN}")
message(STATUS "  OpenGL:  ${HAACK_ENABLE_OPENGL}")
message(STATUS "  NCNN AI: ${HAACK_ENABLE_NCNN}")
message(STATUS "  Netplay: ${HAACK_ENABLE_NETPLAY}")
message(STATUS "  Android: ${HAACK_BUILD_ANDROID}")
message(STATUS "────────────────────────────────────────")
