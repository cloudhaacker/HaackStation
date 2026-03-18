cmake_minimum_required(VERSION 3.20)

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
endif()
