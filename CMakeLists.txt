cmake_minimum_required(VERSION 3.10...3.24)

project(mempool LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

# ============================================
# Options for enabling/disabling components
# ============================================
option(MEMPOOL_BUILD_BENCHMARK "Build benchmark executables" ON)
option(MEMPOOL_BUILD_TESTS "Build unit tests" ON)
option(MEMPOOL_BUILD_EXAMPLES "Build example programs" OFF)
option(MEMPOOL_ENABLE_ASSERTIONS "Enable runtime assertions (debug builds)" ON)
option(MEMPOOL_ENABLE_COVERAGE "Enable code coverage reporting" OFF)

if(MEMPOOL_ENABLE_ASSERTIONS)
    add_compile_definitions(MEMPOOL_ENABLE_ASSERTIONS=1)
else()
    add_compile_definitions(MEMPOOL_ENABLE_ASSERTIONS=0)
endif()

# ============================================
# Main library
# ============================================
add_library(mempool
    mempool.cpp
    mempool.h
    mempool-inl.hpp
)

target_include_directories(mempool 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(Threads REQUIRED)
target_link_libraries(mempool PRIVATE Threads::Threads)

set_target_properties(mempool PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

# ============================================
# Benchmark
# ============================================
if(MEMPOOL_BUILD_BENCHMARK)
    message(STATUS "Building benchmarks")
    include(FetchContent)
    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG         v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(google_benchmark)
    
    add_executable(mempool_bench benchmark/bench_main.cpp)
    target_link_libraries(mempool_bench PRIVATE mempool benchmark::benchmark)
    set_target_properties(mempool_bench PROPERTIES OUTPUT_NAME "mempool-bench")
    
    add_custom_target(run_benchmark
        COMMAND $<TARGET_FILE:mempool_bench>
        COMMENT "Running memory pool benchmarks"
        VERBATIM
    )
endif()

# ============================================
# Unit Tests
# ============================================
if(MEMPOOL_BUILD_TESTS)
    message(STATUS "Building unit tests")
    if(NOT TARGET googletest)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    include(GoogleTest)

    set(TEST_MODULES "tests/test_basic.cpp" "tests/test_concurrency.cpp")
    
    foreach(TEST_SOURCE ${TEST_MODULES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        target_link_libraries(${TEST_NAME} PRIVATE mempool GTest::gtest_main)
        
        gtest_discover_tests(${TEST_NAME}
            TEST_PREFIX "${TEST_NAME}."
            PROPERTIES DISCOVERY_TIMEOUT 10
        )

        if(MEMPOOL_ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
            target_compile_options(${TEST_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
            target_link_options(${TEST_NAME} PRIVATE --coverage)
        endif()
    endforeach()

    add_custom_target(run_all_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all memory pool test modules"
    )
endif()

# ============================================
# Examples
# ============================================
if(MEMPOOL_BUILD_EXAMPLES)
    add_executable(example_basic examples/basic_usage.cpp)
    target_link_libraries(example_basic PRIVATE mempool)
endif()

# ============================================
# Installation & Export Config
# ============================================
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY AnyNewerVersion
)

install(TARGETS mempool
    EXPORT mempoolTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES mempool.h mempool-inl.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT mempoolTargets
    FILE mempoolTargets.cmake
    NAMESPACE mempool::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mempool
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mempoolConfig.cmake.in")
    configure_file(cmake/mempoolConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfig.cmake" @ONLY)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mempool
    )
endif()


# ============================================
# Summary Output
# ============================================
message(STATUS "==========================================")
message(STATUS "Memory Pool Build Configuration Summary")
message(STATUS "==========================================")
message(STATUS "Build components (but NOT installing tests/benchmarks)")
message(STATUS "Tests:      ${MEMPOOL_BUILD_TESTS}")
message(STATUS "Benchmarks: ${MEMPOOL_BUILD_BENCHMARK}")
message(STATUS "==========================================")