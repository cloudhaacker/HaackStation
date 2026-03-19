#!/bin/bash
# HaackStation Windows Build Setup — Visual Studio edition
# Run this from Git Bash inside your HaackStation folder:
#   cd /c/Users/digit/Documents/HaackStation
#   bash build_setup.sh

BASE="$(pwd)"
DEPS_DIR="$BASE/deps"
BUILD_DIR="$BASE/build"

echo "============================================"
echo "  HaackStation Build Setup"
echo "  Compiler: Visual Studio"
echo "  Working in: $BASE"
echo "============================================"
echo ""

# ── Sanity check ──────────────────────────────────────────────────────────────
if [ ! -f "$BASE/CMakeLists.txt" ]; then
    echo "ERROR: Run this from inside your HaackStation folder."
    echo "  cd /c/Users/digit/Documents/HaackStation"
    echo "  bash build_setup.sh"
    exit 1
fi

# ── Detect Visual Studio version ──────────────────────────────────────────────
echo "[1/4] Detecting Visual Studio..."

VS_GENERATOR=""
VS_PATHS=(
    "/c/Program Files/Microsoft Visual Studio/2026/Community/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files/Microsoft Visual Studio/2026/Professional/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files/Microsoft Visual Studio/2026/Enterprise/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files (x86)/Microsoft Visual Studio/2019/Community/MSBuild/Current/Bin/MSBuild.exe"
    "/c/Program Files (x86)/Microsoft Visual Studio/2019/Professional/MSBuild/Current/Bin/MSBuild.exe"
)

for p in "${VS_PATHS[@]}"; do
    if [ -f "$p" ]; then
        if [[ "$p" == *"2026"* ]]; then
            VS_GENERATOR="Visual Studio 18 2026"
        elif [[ "$p" == *"2022"* ]]; then
            VS_GENERATOR="Visual Studio 17 2022"
        else
            VS_GENERATOR="Visual Studio 16 2019"
        fi
        echo "  Found: $VS_GENERATOR"
        break
    fi
done

if [ -z "$VS_GENERATOR" ]; then
    echo ""
    echo "  Could not auto-detect Visual Studio."
    echo "  Which version do you have?"
    echo "  1) Visual Studio 2026"
    echo "  2) Visual Studio 2022"
    echo "  3) Visual Studio 2019"
    echo "  4) Visual Studio 2017"
    read -p "  Enter 1, 2, 3, or 4: " VS_CHOICE
    case "$VS_CHOICE" in
        1) VS_GENERATOR="Visual Studio 18 2026" ;;
        2) VS_GENERATOR="Visual Studio 17 2022" ;;
        3) VS_GENERATOR="Visual Studio 16 2019" ;;
        4) VS_GENERATOR="Visual Studio 15 2017" ;;
        *) VS_GENERATOR="Visual Studio 18 2026" ;;
    esac
    echo "  Using: $VS_GENERATOR"
fi
echo ""

# ── Check CMake ───────────────────────────────────────────────────────────────
echo "[2/4] Checking CMake..."
if ! command -v cmake &>/dev/null; then
    CMAKE_PATHS=(
        "/c/Program Files/CMake/bin"
        "/c/Program Files (x86)/CMake/bin"
    )
    for p in "${CMAKE_PATHS[@]}"; do
        if [ -f "$p/cmake.exe" ]; then
            export PATH="$p:$PATH"
            break
        fi
    done
fi

if ! command -v cmake &>/dev/null; then
    echo "  CMake not found. Download from: https://cmake.org/download/"
    echo "  Choose the .msi installer and tick 'Add to PATH' during install."
    echo "  Then reopen Git Bash and run this script again."
    exit 1
fi
cmake --version | head -1
echo "  OK"
echo ""

# ── Download SDL2 ─────────────────────────────────────────────────────────────
echo "[3/4] Setting up SDL2 and SDL2_image..."
SDL2_VERSION="2.30.2"
SDL2_IMAGE_VERSION="2.8.2"
SDL2_TTF_VERSION="2.22.0"

# For Visual Studio we use the VC (MSVC) development libraries, not MinGW
SDL2_ZIP="SDL2-devel-${SDL2_VERSION}-VC.zip"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/${SDL2_ZIP}"

SDL2_IMAGE_ZIP="SDL2_image-devel-${SDL2_IMAGE_VERSION}-VC.zip"
SDL2_IMAGE_URL="https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VERSION}/${SDL2_IMAGE_ZIP}"

SDL2_TTF_ZIP="SDL2_ttf-devel-${SDL2_TTF_VERSION}-VC.zip"
SDL2_TTF_URL="https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VERSION}/${SDL2_TTF_ZIP}"

mkdir -p "$DEPS_DIR"

download_and_extract() {
    local name="$1"
    local url="$2"
    local zip="$3"
    local dest="$4"

    if [ -d "$dest" ]; then
        echo "  $name: already present, skipping."
        return
    fi

    echo "  Downloading $name..."
    if command -v curl &>/dev/null; then
        curl -L "$url" -o "$DEPS_DIR/$zip" --progress-bar
    else
        wget "$url" -O "$DEPS_DIR/$zip" -q --show-progress
    fi

    echo "  Extracting $name..."
    cd "$DEPS_DIR"
    unzip -q "$zip" -d "$name"
    # The zip contains a versioned subfolder — flatten it
    INNER=$(find "$DEPS_DIR/$name" -maxdepth 1 -mindepth 1 -type d | head -1)
    if [ -n "$INNER" ] && [ "$INNER" != "$dest" ]; then
        mv "$INNER"/* "$DEPS_DIR/$name/" 2>/dev/null || true
    fi
    cd "$BASE"
    echo "  $name: OK"
}

download_and_extract "SDL2"       "$SDL2_URL"       "$SDL2_ZIP"       "$DEPS_DIR/SDL2"
download_and_extract "SDL2_image" "$SDL2_IMAGE_URL" "$SDL2_IMAGE_ZIP" "$DEPS_DIR/SDL2_image"
download_and_extract "SDL2_ttf"   "$SDL2_TTF_URL"   "$SDL2_TTF_ZIP"   "$DEPS_DIR/SDL2_ttf"
echo ""

# ── Configure with CMake ──────────────────────────────────────────────────────
echo "[4/4] Configuring CMake..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$BASE" \
    -G "$VS_GENERATOR" \
    -A x64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL2_DIR="$DEPS_DIR/SDL2" \
    -DSDL2IMAGE_DIR="$DEPS_DIR/SDL2_image" \
    -DSDL2TTF_DIR="$DEPS_DIR/SDL2_ttf" \
    -DCMAKE_PREFIX_PATH="$DEPS_DIR/SDL2;$DEPS_DIR/SDL2_image;$DEPS_DIR/SDL2_ttf"

if [ $? -ne 0 ]; then
    echo ""
    echo "============================================"
    echo "  CMake configuration failed."
    echo "  Copy the red error text above and share"
    echo "  it so we can fix it."
    echo "============================================"
    exit 1
fi

echo ""
echo "  Configuration successful!"
echo ""

# ── Build ─────────────────────────────────────────────────────────────────────
echo "  Building HaackStation (this takes a few minutes)..."
echo ""

cmake --build . --config Release

if [ $? -ne 0 ]; then
    echo ""
    echo "============================================"
    echo "  Build failed."
    echo "  Copy the error text above and share it."
    echo "============================================"
    exit 1
fi

echo ""
echo "============================================"
echo "  BUILD SUCCESSFUL!"
echo ""
echo "  Copying runtime files to build output..."

RELEASE_DIR="$BUILD_DIR/Release"

# Copy SDL2 DLLs next to the exe
cp "$DEPS_DIR/SDL2/lib/x64/SDL2.dll"           "$RELEASE_DIR/" 2>/dev/null || true
cp "$DEPS_DIR/SDL2_image/lib/x64/SDL2_image.dll" "$RELEASE_DIR/" 2>/dev/null || true
cp "$DEPS_DIR/SDL2_ttf/lib/x64/SDL2_ttf.dll"   "$RELEASE_DIR/" 2>/dev/null || true

# Copy the core
mkdir -p "$RELEASE_DIR/core"
cp "$BASE/core/"*.dll "$RELEASE_DIR/core/" 2>/dev/null || true

# Copy assets
cp -r "$BASE/assets" "$RELEASE_DIR/" 2>/dev/null || true

# Copy bios folder structure (not the actual BIOS files — those stay private)
mkdir -p "$RELEASE_DIR/bios"
mkdir -p "$RELEASE_DIR/saves"

echo ""
echo "  Executable: build/Release/HaackStation.exe"
echo ""
echo "  Before running, copy your BIOS files:"
echo "  bios/scph1001.bin  ->  build/Release/bios/scph1001.bin"
echo ""
echo "  Then double-click HaackStation.exe or run:"
echo "  cd build/Release"
echo "  ./HaackStation.exe"
echo "============================================"
