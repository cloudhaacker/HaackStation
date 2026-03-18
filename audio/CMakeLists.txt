cmake_minimum_required(VERSION 3.20)
add_library(HaackAudio STATIC src/audio_replacer.cpp)
target_include_directories(HaackAudio PUBLIC include)
target_link_libraries(HaackAudio PUBLIC SDL2::SDL2)
