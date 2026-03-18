#!/bin/bash
# HaackStation Setup Script
# Run this from inside your HaackStation folder:
#   cd /c/Users/digit/Documents/HaackStation
#   bash setup.sh
#
# This script:
#   1. Creates all required subfolders
#   2. Moves any flat files to their correct locations
#   3. Creates any files that are missing
#   4. Initializes Git and makes your first commit

set -e
BASE="$(pwd)"

echo "============================================"
echo "  HaackStation Setup"
echo "  Working in: $BASE"
echo "============================================"
echo ""

# ── 1. Create all folders ─────────────────────────────────────────────────────
echo "[1/5] Creating folder structure..."
mkdir -p frontend/include/ui
mkdir -p frontend/include/library
mkdir -p frontend/include/renderer
mkdir -p frontend/include/core_bridge
mkdir -p frontend/src/ui
mkdir -p frontend/src/library
mkdir -p frontend/src/renderer
mkdir -p audio/include
mkdir -p audio/src
mkdir -p textures/include
mkdir -p textures/src
mkdir -p shaders/include
mkdir -p shaders/src
mkdir -p netplay
mkdir -p docs
mkdir -p build
mkdir -p assets/icons
echo "    Done."
echo ""

# ── 2. Move flat files to correct locations ───────────────────────────────────
echo "[2/5] Moving files to correct locations..."

move_if_exists() {
    if [ -f "$BASE/$1" ]; then
        mv "$BASE/$1" "$BASE/$2"
        echo "    Moved: $1 -> $2"
    fi
}

# Docs
move_if_exists "HaackStation_DevDiary.docx"   "docs/HaackStation_DevDiary.docx"
move_if_exists "GitHub_Setup_Guide.docx"       "docs/GitHub_Setup_Guide.docx"

# Source files -> src folders
move_if_exists "app.cpp"              "frontend/src/app.cpp"
move_if_exists "main.cpp"             "frontend/src/main.cpp"
move_if_exists "controller_nav.cpp"   "frontend/src/ui/controller_nav.cpp"
move_if_exists "disc_formats.cpp"     "frontend/src/library/disc_formats.cpp"
move_if_exists "game_scanner.cpp"     "frontend/src/library/game_scanner.cpp"
move_if_exists "game_browser.cpp"     "frontend/src/ui/game_browser.cpp"
move_if_exists "settings_screen.cpp"  "frontend/src/ui/settings_screen.cpp"
move_if_exists "theme_engine.cpp"     "frontend/src/renderer/theme_engine.cpp"

# Header files -> include folders
move_if_exists "app.h"                "frontend/include/app.h"
move_if_exists "controller_nav.h"     "frontend/include/ui/controller_nav.h"
move_if_exists "disc_formats.h"       "frontend/include/library/disc_formats.h"
move_if_exists "game_scanner.h"       "frontend/include/library/game_scanner.h"
move_if_exists "game_browser.h"       "frontend/include/ui/game_browser.h"
move_if_exists "settings_screen.h"    "frontend/include/ui/settings_screen.h"
move_if_exists "theme_engine.h"       "frontend/include/renderer/theme_engine.h"
move_if_exists "libretro_bridge.h"    "frontend/include/core_bridge/libretro_bridge.h"
move_if_exists "audio_replacer.h"     "audio/include/audio_replacer.h"
move_if_exists "texture_replacer.h"   "textures/include/texture_replacer.h"
move_if_exists "shader_manager.h"     "shaders/include/shader_manager.h"

echo "    Done."
echo ""

# ── 3. Write missing files that weren't downloaded ───────────────────────────
echo "[3/5] Creating any missing files..."

write_if_missing() {
    local filepath="$1"
    local content="$2"
    if [ ! -f "$BASE/$filepath" ]; then
        echo "$content" > "$BASE/$filepath"
        echo "    Created: $filepath"
    else
        echo "    Already exists: $filepath (skipped)"
    fi
}

# .gitignore
write_if_missing ".gitignore" '# Build directories
build/
out/
cmake-build-*/

# IDE
.vs/
.vscode/settings.json
*.user
*.suo
.idea/

# Compiled output
*.exe
*.dll
*.so
*.dylib
*.a
*.lib

# CMake generated
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
Makefile
!CMakeLists.txt

# BIOS - NEVER commit (copyrighted)
bios/

# Game files - NEVER commit
*.iso
*.chd
*.cue
*.img

# Saves
*.mcr
*.mcd
*.srm
*.state

# OS
.DS_Store
Thumbs.db

# Node tooling
node_modules/
package-lock.json'

# frontend/CMakeLists.txt
write_if_missing "frontend/CMakeLists.txt" 'cmake_minimum_required(VERSION 3.20)

set(FRONTEND_SOURCES
    src/main.cpp
    src/app.cpp
    src/ui/game_browser.cpp
    src/ui/controller_nav.cpp
    src/ui/settings_screen.cpp
    src/library/game_scanner.cpp
    src/library/disc_formats.cpp
    src/renderer/theme_engine.cpp
)

add_executable(HaackStation \${FRONTEND_SOURCES})

target_include_directories(HaackStation PRIVATE
    include
    \${SDL2_INCLUDE_DIRS}
    \${CMAKE_BINARY_DIR}/include
    ../audio/include
    ../textures/include
    ../shaders/include
)

target_link_libraries(HaackStation PRIVATE
    SDL2::SDL2
    HaackAudio
    HaackTextures
    HaackShaders
)

if(WIN32)
    target_link_libraries(HaackStation PRIVATE SDL2::SDL2main)
endif()'

# audio/CMakeLists.txt
write_if_missing "audio/CMakeLists.txt" 'cmake_minimum_required(VERSION 3.20)
add_library(HaackAudio STATIC src/audio_replacer.cpp)
target_include_directories(HaackAudio PUBLIC include)
target_link_libraries(HaackAudio PUBLIC SDL2::SDL2)'

# audio/src/audio_replacer.cpp (stub)
write_if_missing "audio/src/audio_replacer.cpp" '#include "audio_replacer.h"
// Audio replacement implementation - Phase 3
AudioReplacer::AudioReplacer()  {}
AudioReplacer::~AudioReplacer() {}
bool AudioReplacer::loadPackForGame(const std::string&) { return false; }
void AudioReplacer::unloadPack() {}
bool AudioReplacer::processAudioFrame(int16_t*, int) { return false; }
void AudioReplacer::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }'

# textures/CMakeLists.txt
write_if_missing "textures/CMakeLists.txt" 'cmake_minimum_required(VERSION 3.20)
add_library(HaackTextures STATIC src/texture_replacer.cpp)
target_include_directories(HaackTextures PUBLIC include)
find_package(PNG QUIET)
if(PNG_FOUND)
    target_compile_definitions(HaackTextures PRIVATE HAACK_PNG)
    target_link_libraries(HaackTextures PRIVATE PNG::PNG)
endif()'

# textures/src/texture_replacer.cpp (stub)
write_if_missing "textures/src/texture_replacer.cpp" '#include "texture_replacer.h"
// Texture replacement implementation - Phase 3
TextureReplacer::TextureReplacer()  {}
TextureReplacer::~TextureReplacer() {}
bool TextureReplacer::loadPackForGame(const std::string&) { return false; }
void TextureReplacer::unloadPack() {}
const ReplacementTexture* TextureReplacer::getReplacementTexture(uint64_t) { return nullptr; }
void TextureReplacer::notifyTextureUsed(uint64_t) {}
void TextureReplacer::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }'

# shaders/CMakeLists.txt
write_if_missing "shaders/CMakeLists.txt" 'cmake_minimum_required(VERSION 3.20)
add_library(HaackShaders STATIC src/shader_manager.cpp)
target_include_directories(HaackShaders PUBLIC include)'

# shaders/src/shader_manager.cpp (stub)
write_if_missing "shaders/src/shader_manager.cpp" '#include "shader_manager.h"
#include <filesystem>
// Shader manager implementation - Phase 3
ShaderManager::ShaderManager()  {}
ShaderManager::~ShaderManager() {}
void ShaderManager::scanShaderPacks(const std::string&) {}
bool ShaderManager::applyPreset(const std::string&) { return false; }
bool ShaderManager::applyBuiltin(BuiltinShader s) { m_activeBuiltin = s; return true; }
void ShaderManager::clearShader() { m_activeBuiltin = BuiltinShader::NONE; m_activePreset = nullptr; }
void ShaderManager::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }'

# version.h.in
write_if_missing "frontend/include/version.h.in" '#pragma once
#define HAACK_VERSION_MAJOR @HaackStation_VERSION_MAJOR@
#define HAACK_VERSION_MINOR @HaackStation_VERSION_MINOR@
#define HAACK_VERSION_PATCH @HaackStation_VERSION_PATCH@
#define HAACK_VERSION_STRING "@HaackStation_VERSION@"
#define HAACK_APP_NAME      "HaackStation"
#define HAACK_CORE_NAME     "Beetle PSX HW"
#define HAACK_CORE_LICENSE  "GPLv2"'

echo "    Done."
echo ""

# ── 4. Verify structure ───────────────────────────────────────────────────────
echo "[4/5] Verifying structure..."
echo ""
echo "Your HaackStation folder now contains:"
find "$BASE" -not -path '*/build/*' -not -path '*/.git/*' \
    -not -name '*.docx' | sort | sed "s|$BASE/||" | sed 's|[^/]*/|  |g'
echo ""

# ── 5. Git setup ─────────────────────────────────────────────────────────────
echo "[5/5] Setting up Git..."
echo ""

if [ -d "$BASE/.git" ]; then
    echo "    Git already initialized."
else
    git init
    echo "    Git initialized."
fi

# Check if remote exists
if git remote get-url origin &>/dev/null; then
    echo "    Remote 'origin' already set: $(git remote get-url origin)"
else
    echo ""
    echo "    ACTION NEEDED: Enter your GitHub repo URL below."
    echo "    It looks like: https://github.com/YourUsername/HaackStation.git"
    read -p "    GitHub URL: " GITHUB_URL
    if [ -n "$GITHUB_URL" ]; then
        git remote add origin "$GITHUB_URL"
        echo "    Remote set to: $GITHUB_URL"
    else
        echo "    Skipped (you can add it later with: git remote add origin <url>)"
    fi
fi

echo ""
git add .
git status --short
echo ""
echo "============================================"
echo "  Ready to commit!"
echo "  Run these two commands:"
echo ""
echo '  git commit -m "Day 1+2: Project foundation, UI layer, folder structure"'
echo "  git push -u origin main"
echo ""
echo "  If git push fails, see the note below about"
echo "  Personal Access Tokens."
echo "============================================"
