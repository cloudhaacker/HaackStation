#!/bin/bash
# HaackStation Include Fixer
# Run from inside your HaackStation folder:
#   cd /c/Users/digit/Documents/HaackStation
#   bash fix_includes.sh

BASE="$(pwd)"

if [ ! -f "$BASE/CMakeLists.txt" ]; then
    echo "ERROR: Run this from your HaackStation folder"
    exit 1
fi

echo "Fixing all include paths..."

# ── Helper function ───────────────────────────────────────────────────────────
fix_file() {
    local file="$1"
    if [ ! -f "$file" ]; then
        echo "  SKIP (not found): $file"
        return
    fi
    # Run all replacements on this file using sed
    sed -i \
        's|#include "ui/controller_nav.h"|#include "controller_nav.h"|g' \
        's|#include "ui/game_browser.h"|#include "game_browser.h"|g' \
        's|#include "ui/settings_screen.h"|#include "settings_screen.h"|g' \
        's|#include "ui/splash_screen.h"|#include "splash_screen.h"|g' \
        's|#include "library/disc_formats.h"|#include "disc_formats.h"|g' \
        's|#include "library/game_scanner.h"|#include "game_scanner.h"|g' \
        's|#include "renderer/theme_engine.h"|#include "theme_engine.h"|g' \
        's|#include "core_bridge/libretro_bridge.h"|#include "libretro_bridge.h"|g' \
        's|#include "core_bridge/libretro_types.h"|#include "libretro_types.h"|g' \
        's|#include "audio/audio_replacer.h"|#include "audio_replacer.h"|g' \
        's|#include "textures/texture_replacer.h"|#include "texture_replacer.h"|g' \
        "$file"
    echo "  Fixed: $file"
}

# Fix every .h and .cpp file in the project
find "$BASE/frontend" -name "*.h" -o -name "*.cpp" | while read f; do
    fix_file "$f"
done

echo ""
echo "Fixing CMakeLists files..."

# ── Rewrite frontend/CMakeLists.txt completely ────────────────────────────────
cat > "$BASE/frontend/CMakeLists.txt" << 'CMAKE_EOF'
cmake_minimum_required(VERSION 3.20)

set(FRONTEND_SOURCES
    src/main.cpp
    src/app.cpp
    src/ui/game_browser.cpp
    src/ui/controller_nav.cpp
    src/ui/settings_screen.cpp
    src/ui/splash_screen.cpp
    src/library/game_scanner.cpp
    src/library/disc_formats.cpp
    src/renderer/theme_engine.cpp
    src/core_bridge/libretro_bridge.cpp
)

add_executable(HaackStation ${FRONTEND_SOURCES})

target_include_directories(HaackStation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/library
    ${CMAKE_CURRENT_SOURCE_DIR}/include/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core_bridge
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2_image/include
    ${CMAKE_SOURCE_DIR}/audio/include
    ${CMAKE_SOURCE_DIR}/textures/include
    ${CMAKE_SOURCE_DIR}/shaders/include
)

target_link_libraries(HaackStation PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    HaackAudio
    HaackTextures
    HaackShaders
)

if(SDL2_TTF_FOUND OR EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/lib/x64/SDL2_ttf.lib")
    target_link_libraries(HaackStation PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/lib/x64/SDL2_ttf.lib")
endif()

if(SDL2_IMAGE_FOUND OR EXISTS "${CMAKE_SOURCE_DIR}/deps/SDL2_image/lib/x64/SDL2_image.lib")
    target_link_libraries(HaackStation PRIVATE
        "${CMAKE_SOURCE_DIR}/deps/SDL2_image/lib/x64/SDL2_image.lib")
    target_compile_definitions(HaackStation PRIVATE HAACK_SDL_IMAGE)
endif()

if(WIN32)
    set_target_properties(HaackStation PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>)
    target_link_libraries(HaackStation PRIVATE
        winmm imm32 setupapi version)
endif()

add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:HaackStation>/assets"
    COMMENT "Copying assets to build output"
)
CMAKE_EOF
echo "  Rewrote: frontend/CMakeLists.txt"

# ── Rewrite root CMakeLists.txt completely ────────────────────────────────────
cat > "$BASE/CMakeLists.txt" << 'ROOTCMAKE_EOF'
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
ROOTCMAKE_EOF
echo "  Rewrote: CMakeLists.txt"

# ── Fix module CMakeLists ─────────────────────────────────────────────────────
cat > "$BASE/audio/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.20)
add_library(HaackAudio STATIC src/audio_replacer.cpp)
target_include_directories(HaackAudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(HaackAudio PUBLIC SDL2::SDL2)
EOF

cat > "$BASE/textures/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.20)
add_library(HaackTextures STATIC src/texture_replacer.cpp)
target_include_directories(HaackTextures PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
EOF

cat > "$BASE/shaders/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.20)
add_library(HaackShaders STATIC src/shader_manager.cpp)
target_include_directories(HaackShaders PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
EOF
echo "  Rewrote: audio/CMakeLists.txt"
echo "  Rewrote: textures/CMakeLists.txt"
echo "  Rewrote: shaders/CMakeLists.txt"

# ── Verify SDL2 deps exist ────────────────────────────────────────────────────
echo ""
echo "Checking deps folder..."
if [ -f "$BASE/deps/SDL2/include/SDL.h" ]; then
    echo "  SDL2 headers: OK"
elif [ -f "$BASE/deps/SDL2/include/SDL2/SDL.h" ]; then
    echo "  SDL2 headers found in SDL2 subfolder — fixing..."
    # The zip extracted with an SDL2 subfolder inside include
    # Move them up one level
    cp -r "$BASE/deps/SDL2/include/SDL2/"* "$BASE/deps/SDL2/include/"
    echo "  SDL2 headers: Fixed"
else
    echo "  WARNING: SDL2 headers not found at deps/SDL2/include/SDL.h"
    echo "  Check your deps folder structure."
    echo "  Expected: deps/SDL2/include/SDL.h"
fi

echo ""
echo "============================================"
echo "  All fixes applied!"
echo "  Now run:"
echo "    rm -rf build"
echo "    bash build_setup.sh"
echo "============================================"
