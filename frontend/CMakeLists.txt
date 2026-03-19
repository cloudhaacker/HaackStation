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
    include
    ${SDL2_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/include
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

if(HAACK_ENABLE_NETPLAY)
    target_link_libraries(HaackStation PRIVATE HaackNetplay)
endif()

if(WIN32)
    target_link_libraries(HaackStation PRIVATE SDL2::SDL2main)
    set_target_properties(HaackStation PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ── Copy assets to build directory ────────────────────────────────────────────
# Font and logo are loaded at runtime relative to the executable.
# This copies assets/ next to HaackStation.exe after every build.
add_custom_command(TARGET HaackStation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:HaackStation>/assets"
    COMMENT "Copying assets (fonts, icons) to build output"
)
