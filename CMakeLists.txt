cmake_minimum_required(VERSION 3.20)
project(HaackStation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HAACK_BUILD_ANDROID  "Build for Android" OFF)
option(HAACK_ENABLE_NETPLAY "Enable netplay"    OFF)

# ── SDL2 setup ─────────────────────────────────────────────────────────────────
set(SDL2_BASE "${CMAKE_SOURCE_DIR}/deps/SDL2")
set(SDL2_INC  "${SDL2_BASE}/include")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL2_LIB_DIR "${SDL2_BASE}/lib/x64")
else()
    set(SDL2_LIB_DIR "${SDL2_BASE}/lib/x86")
endif()

# Make SDL2/SDL.h findable by ALL targets globally
include_directories("${SDL2_INC}")
include_directories("${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/include")
include_directories("${CMAKE_SOURCE_DIR}/deps/SDL2_image/include")

# SDL2 imported targets
if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INC}")
endif()

if(NOT TARGET SDL2::SDL2main)
    add_library(SDL2::SDL2main UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2main PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2main.lib")
endif()

set(SDL2_INCLUDE_DIRS "${SDL2_INC}")
set(SDL2_TTF_FOUND TRUE)
set(SDL2_IMAGE_FOUND TRUE)

message(STATUS "SDL2 include: ${SDL2_INC}")
message(STATUS "SDL2 lib:     ${SDL2_LIB_DIR}")

# ── Core check ─────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/core/mednafen_psx_hw_libretro.dll")
    message(STATUS "Core: mednafen_psx_hw_libretro.dll found")
else()
    message(WARNING "Core DLL not found in core/ — needed at runtime")
endif()

# ── Subdirectories ─────────────────────────────────────────────────────────────
add_subdirectory(frontend)
add_subdirectory(audio)
add_subdirectory(textures)
add_subdirectory(shaders)

# ── Version header ─────────────────────────────────────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/frontend/include/version.h.in"
    "${CMAKE_BINARY_DIR}/include/version.h"
)

message(STATUS "HaackStation ${PROJECT_VERSION} configured")
