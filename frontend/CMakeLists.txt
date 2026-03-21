cmake_minimum_required(VERSION 3.20)

# ── rcheevos sources ──────────────────────────────────────────────────────────
set(RCHEEVOS_DIR "${CMAKE_SOURCE_DIR}/deps/rcheevos")
# Glob all rcheevos .c files - exclude rc_libretro.c (needs libretro.h, not needed)
file(GLOB_RECURSE RCHEEVOS_SOURCES "${RCHEEVOS_DIR}/src/*.c")
list(FILTER RCHEEVOS_SOURCES EXCLUDE REGEX "rc_libretro\.c$")
message(STATUS "rcheevos sources: ${RCHEEVOS_SOURCES}")

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
    src/settings_manager.cpp
    src/per_game_settings.cpp
    src/memcard_manager.cpp
    src/game_scraper.cpp
    src/scrape_screen.cpp
    src/save_state_manager.cpp
    src/ingame_menu.cpp
    src/ra_manager.cpp
)

add_executable(HaackStation ${FRONTEND_SOURCES})

# Explicit lib paths for MSVC — variables from root CMakeLists
set(SDL2_TTF_LIB   "${CMAKE_SOURCE_DIR}/deps/SDL2_ttf/lib/x64/SDL2_ttf.lib")
set(SDL2_IMAGE_LIB "${CMAKE_SOURCE_DIR}/deps/SDL2_image/lib/x64/SDL2_image.lib")

target_sources(HaackStation PRIVATE ${RCHEEVOS_SOURCES})

target_include_directories(HaackStation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/library
    ${CMAKE_CURRENT_SOURCE_DIR}/include/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core_bridge
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/SDL2/include
    ${RCHEEVOS_DIR}/include
    ${RCHEEVOS_DIR}/src
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
    "${SDL2_TTF_LIB}"
    "${SDL2_IMAGE_LIB}"
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

# Copy SDL2 DLLs — runs after every build so clean rebuilds don't break things
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

# Create bios and saves folders
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/bios"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/saves"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/memcards/shared"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:HaackStation>/memcards/per_game"
    COMMENT "Creating runtime directories"
)
