cmake_minimum_required(VERSION 3.20)
add_library(HaackAudio STATIC src/audio_replacer.cpp)
target_include_directories(HaackAudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(HaackAudio PUBLIC SDL2::SDL2)
