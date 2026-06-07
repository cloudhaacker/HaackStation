cmake_minimum_required(VERSION 3.20)

# ── rcheevos sources ──────────────────────────────────────────────────────────
set(RCHEEVOS_DIR "${CMAKE_SOURCE_DIR}/deps/rcheevos")
file(GLOB_RECURSE RCHEEVOS_SOURCES "${RCHEEVOS_DIR}/src/*.c")
list(FILTER RCHEEVOS_SOURCES EXCLUDE REGEX "rc_libretro\.c$")
message(STATUS "rcheevos sources: ${RCHEEVOS_SOURCES}")

# ── SoundTouch sources ────────────────────────────────────────────────────────
set(SOUNDTOUCH_DIR "${CMAKE_SOURCE_DIR}/deps/soundtouch")
file(GLOB SOUNDTOUCH_SOURCES "${SOUNDTOUCH_DIR}/src/*.cpp")
message(STATUS "SoundTouch sources: ${SOUNDTOUCH_SOURCES}")

set(FRONTEND_SOURCES
    src/main.cpp
    src/app.cpp
    src/play_history.cpp
    src/ui/game_browser.cpp
    src/ui/controller_nav.cpp
	src/ui/onscreen_keyboard.cpp
    src/ui/settings_screen.cpp
    src/ui/splash_screen.cpp
	src/ui/haack_hub.cpp
    src/library/game_scanner.cpp
    src/library/disc_formats.cpp
    src/renderer/theme_engine.cpp
    src/core_bridge/libretro_bridge.cpp
    src/settings_manager.cpp
    src/per_game_settings.cpp
	src/per_game_settings_screen.cpp
    src/memcard_manager.cpp
    src/game_scraper.cpp
    src/scrape_screen.cpp
    src/save_state_manager.cpp
    src/ingame_menu.cpp
    src/ra_manager.cpp
    src/game_details_panel.cpp
	src/rewind_manager.cpp
    src/ui/input_map.cpp
    src/ui/remap_screen.cpp
    src/ui/trophy_room.cpp
    src/ui/trophy_hub.cpp
	src/ui/omnisave_vault.cpp
    src/ui/omnisave_import.cpp
	src/ui/omnisave_card_shelf.cpp
    src/ambient_music.cpp           # ← NEW: ambient music player
    src/chd_converter.cpp           # ← NEW: CHD conversion wrapper
)

add_executable(HaackStation ${FRONTEND_SOURCES})

# Explicit lib paths for MSVC — variables from root CMakeLists
set(SDL2_TTF_LIB    "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/lib/x64/SDL2_ttf.lib")
set(SDL2_IMAGE_LIB  "${CMAKE_SOURCE_DIR}/deps/SDL2_image/lib/x64/SDL2_image.lib")
set(SDL2_MIXER_LIB  "${CMAKE_SOURCE_DIR}/deps/SDL2_mixer/lib/x64/SDL2_mixer.lib")

target_sources(HaackStation PRIVATE ${RCHEEVOS_SOURCES} ${SOUNDTOUCH_SOURCES})

# ── Per-file compile flags for rcheevos — CHD support ─────────────────────────
set_source_files_properties(${RCHEEVOS_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "HAVE_CHD=1;RC_HASH_STANDALONE=1;RC_CLIENT_SUPPORTS_HASH=1"
)

# ── libchdr (needed for rcheevos to hash .chd files) ─────────────────────────
set(LIBCHDR_DIR "${CMAKE_SOURCE_DIR}/deps/libchdr")
if(EXISTS "${LIBCHDR_DIR}/include/libchdr/chd.h")
    message(STATUS "libchdr found — CHD hashing enabled")
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
    set(INSTALL_TARGETS     OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LIBCHDR_DIR}" "${CMAKE_BINARY_DIR}/libchdr" EXCLUDE_FROM_ALL)
    target_include_directories(HaackStation PRIVATE "${LIBCHDR_DIR}/include")
    target_link_libraries(HaackStation PRIVATE chdr-static)
    target_compile_definitions(HaackStation PRIVATE HAVE_LIBCHDR=1)

    set_source_files_properties(${RCHEEVOS_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${LIBCHDR_DIR}/include"
    )
else()
    message(STATUS "libchdr NOT found — CHD hashing disabled (BIN/CUE still works)")
    message(STATUS "  To enable: download https://github.com/rtissera/libchdr")
endif()

target_compile_definitions(HaackStation PRIVATE
    HAVE_CHD=1
    RC_HASH_STANDALONE=1
    RC_CLIENT_SUPPORTS_HASH=1
)

target_include_directories(HaackStation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/library
    ${CMAKE_CURRENT_SOURCE_DIR}/include/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core_bridge
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2/include
    ${CMAKE_SOURCE_DIR}/deps/soundtouch/include
    ${CMAKE_SOURCE_DIR}/deps/soundtouch/src
    ${CMAKE_SOURCE_DIR}/deps/libchdr/deps/miniz-3.1.1
    ${CMAKE_SOURCE_DIR}/deps
    ${RCHEEVOS_DIR}/include
    ${RCHEEVOS_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2_image/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2_mixer/include   # ← NEW
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
    "${SDL2_TTF_LIB}"
    "${SDL2_IMAGE_LIB}"
    "${SDL2_MIXER_LIB}"          # ← NEW: ambient music
)

target_compile_definitions(HaackStation PRIVATE HAACK_SDL_IMAGE)

if(WIN32)
    set_target_properties(HaackStation PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>)
    target_link_libraries(HaackStation PRIVATE
        winmm imm32 setupapi version)
endif()

# Copy assets
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:HaackStation>/assets"
    COMMENT "Copying assets"
)

# Copy OSK button hint assets
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/osk"
        "$<TARGET_FILE_DIR:HaackStation>/assets/osk"
    COMMENT "Copying OSK assets"
)

# Copy SDL2 DLLs
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL2/lib/x64/SDL2.dll"
        "$<TARGET_FILE_DIR:HaackStation>/SDL2.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/lib/x64/SDL2_ttf.dll"
        "$<TARGET_FILE_DIR:HaackStation>/SDL2_ttf.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL2_image/lib/x64/SDL2_image.dll"
        "$<TARGET_FILE_DIR:HaackStation>/SDL2_image.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL2_mixer/lib/x64/SDL2_mixer.dll"
        "$<TARGET_FILE_DIR:HaackStation>/SDL2_mixer.dll"   # ← NEW
    COMMENT "Copying SDL2 DLLs"
)

# Copy core DLL
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/core"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/core/mednafen_psx_hw_libretro.dll"
        "$<TARGET_FILE_DIR:HaackStation>/core/mednafen_psx_hw_libretro.dll"
    COMMENT "Copying core DLL"
)

# Create runtime directories
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/bios"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/saves"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/memcards/shared"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/memcards/per_game"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/music"          # ← NEW: ambient music folder
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/tools/chdman"   # ← NEW: chdman download target
    COMMENT "Creating runtime directories"
)
