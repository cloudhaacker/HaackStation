cmake_minimum_required(VERSION 3.20)
add_library(HaackTextures STATIC src/texture_replacer.cpp)
target_include_directories(HaackTextures PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
