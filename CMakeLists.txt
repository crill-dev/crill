cmake_minimum_required(VERSION 3.22)
project(crill)

include_directories(include)

add_executable(tests tests/main.cpp)
target_compile_features(tests PRIVATE cxx_std_17)
