cmake_minimum_required(VERSION 3.10...3.24)
project(mempool LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mempool
  mempool.cpp
  mempool.h
  mempool-inl.hpp
)

target_include_directories(mempool PUBLIC ${CMAKE_CURRENT_LIST_DIR})

find_package(Threads REQUIRED)
target_link_libraries(mempool PRIVATE Threads::Threads)

include(FetchContent)
FetchContent_Declare(
  google_benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        main
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(google_benchmark)


add_executable(mempool_bench benchmark/bench_main.cpp)
target_link_libraries(mempool_bench PRIVATE 
    mempool 
    benchmark::benchmark
)