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
