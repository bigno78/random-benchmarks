cmake_minimum_required(VERSION 3.8)
project(benchmarks LANGUAGES CXX)


add_library(nanobench STATIC libs/nanobench/src/nanobench.cpp)

target_include_directories(nanobench PUBLIC libs/nanobench/include)
target_compile_features(nanobench PUBLIC cxx_std_17)

add_subdirectory(integer-parsing/)
