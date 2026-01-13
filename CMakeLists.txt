cmake_minimum_required(VERSION 3.10...3.24)

project(mempool LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================
# Options for enabling/disabling components
# ============================================
option(MEMPOOL_BUILD_BENCHMARK "Build benchmark executables" ON)
option(MEMPOOL_BUILD_TESTS "Build unit tests" ON)
option(MEMPOOL_BUILD_EXAMPLES "Build example programs" OFF)
option(MEMPOOL_ENABLE_ASSERTIONS "Enable runtime assertions (debug builds)" ON)

# Add definitions for conditional compilation
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

# Optional: set version for the library
set_target_properties(mempool PROPERTIES
  VERSION 1.0.0
  SOVERSION 1
)

# ============================================
# Benchmark (optional)
# ============================================
if(MEMPOOL_BUILD_BENCHMARK)
  message(STATUS "Building benchmarks")
  
  include(FetchContent)
  
  FetchContent_Declare(
    google_benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3  # Use a stable tag instead of main
  )
  
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark installation" FORCE)
  
  FetchContent_MakeAvailable(google_benchmark)
  
  add_executable(mempool_bench benchmark/bench_main.cpp)
  target_link_libraries(mempool_bench PRIVATE 
      mempool 
      benchmark::benchmark
  )
  
  # Set nicer output name
  set_target_properties(mempool_bench PROPERTIES OUTPUT_NAME "mempool-bench")
  
  # Install benchmark if requested
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Don't install by default for local development
  else()
    install(TARGETS mempool_bench
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
  
  # Add a custom target for running benchmarks
  add_custom_target(run_benchmark
    COMMAND $<TARGET_FILE:mempool_bench>
    COMMENT "Running memory pool benchmarks"
    VERBATIM
  )
else()
  message(STATUS "Skipping benchmark build")
endif()

# ============================================
# Unit Tests (optional)
# ============================================
if(MEMPOOL_BUILD_TESTS)
  message(STATUS "Building unit tests")
  
  if(NOT TARGET googletest)  # Only fetch if not already available
    include(FetchContent)
    
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    
    set(gtest_force_shared_crt ON CACHE BOOL "Force shared CRT for gtest" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "Disable gmock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable gtest installation" FORCE)
    
    FetchContent_MakeAvailable(googletest)
  endif()
  
  add_executable(mempool_test tests/test_main.cpp)
  target_link_libraries(mempool_test PRIVATE 
      mempool 
      GTest::gtest_main
  )
  
  # Set nicer output name
  set_target_properties(mempool_test PROPERTIES OUTPUT_NAME "mempool-test")
  
  enable_testing()
  add_test(NAME mempool_unit_tests COMMAND mempool_test)
  
  # Add a custom target for running tests
  add_custom_target(run_tests
    COMMAND $<TARGET_FILE:mempool_test>
    COMMENT "Running memory pool unit tests"
    VERBATIM
  )
  
  # Install test executable if requested
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Don't install by default for local development
  else()
    install(TARGETS mempool_test
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
  
  # Add test coverage if requested
  option(MEMPOOL_ENABLE_COVERAGE "Enable code coverage reporting" OFF)
  if(MEMPOOL_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      message(STATUS "Enabling code coverage")
      target_compile_options(mempool_test PRIVATE --coverage -fprofile-arcs -ftest-coverage)
      target_link_options(mempool_test PRIVATE --coverage)
    else()
      message(WARNING "Code coverage only supported for GCC and Clang")
    endif()
  endif()
else()
  message(STATUS "Skipping unit test build")
endif()

# ============================================
# Examples (optional)
# ============================================
if(MEMPOOL_BUILD_EXAMPLES)
  message(STATUS "Building examples")
  
  # Example 1: Basic usage
  add_executable(example_basic examples/basic_usage.cpp)
  target_link_libraries(example_basic PRIVATE mempool)
  
  # Example 2: Threaded usage
  add_executable(example_threaded examples/threaded_usage.cpp)
  target_link_libraries(example_threaded PRIVATE mempool)
  
  # Install examples if requested
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Don't install by default for local development
  else()
    install(TARGETS example_basic example_threaded
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    )
  endif()
endif()

# ============================================
# Installation
# ============================================
include(GNUInstallDirs)

# Export configuration for find_package support
include(CMakePackageConfigHelpers)

# Create config version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfigVersion.cmake"
  VERSION 1.0.0
  COMPATIBILITY AnyNewerVersion
)

# Install library and headers
install(TARGETS mempool
  EXPORT mempoolTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES mempool.h mempool-inl.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install export set
install(EXPORT mempoolTargets
  FILE mempoolTargets.cmake
  NAMESPACE mempool::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mempool
)

# Install config file
configure_file(cmake/mempoolConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfig.cmake"
  @ONLY
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/mempoolConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mempool
)

# ============================================
# Package generation (optional)
# ============================================
option(MEMPOOL_GENERATE_PACKAGE "Generate package files (CPack)" OFF)
if(MEMPOOL_GENERATE_PACKAGE)
  include(InstallRequiredSystemLibraries)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_PACKAGE_VERSION_MAJOR "1")
  set(CPACK_PACKAGE_VERSION_MINOR "0")
  set(CPACK_PACKAGE_VERSION_PATCH "0")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance memory pool library")
  set(CPACK_PACKAGE_VENDOR "Your Name/Company")
  include(CPack)
endif()

# ============================================
# Summary
# ============================================
message(STATUS "==========================================")
message(STATUS "Memory Pool Build Configuration")
message(STATUS "==========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Tests: ${MEMPOOL_BUILD_TESTS}")
message(STATUS "Benchmarks: ${MEMPOOL_BUILD_BENCHMARK}")
message(STATUS "Examples: ${MEMPOOL_BUILD_EXAMPLES}")
message(STATUS "Assertions: ${MEMPOOL_ENABLE_ASSERTIONS}")
message(STATUS "Coverage: ${MEMPOOL_ENABLE_COVERAGE}")
message(STATUS "Package generation: ${MEMPOOL_GENERATE_PACKAGE}")
message(STATUS "==========================================")