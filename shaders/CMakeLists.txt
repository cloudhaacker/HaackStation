cmake_minimum_required(VERSION 3.20)
add_library(HaackShaders STATIC src/shader_manager.cpp)
target_include_directories(HaackShaders PUBLIC include)
