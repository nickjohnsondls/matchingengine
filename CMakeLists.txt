cmake_minimum_required(VERSION 3.20)
project(MicroMatch VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(CORE_SOURCES
    src/core/orderbook.cpp
    src/core/matching_engine.cpp
)

# Create a library for core components
add_library(micromatch_core STATIC ${CORE_SOURCES})
target_link_libraries(micromatch_core Threads::Threads)

# Find packages
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(benchmark QUIET)

# Enable testing
enable_testing()

# Test executable for orders
add_executable(test_order tests/test_order.cpp)
target_link_libraries(test_order
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME OrderTests COMMAND test_order)

# Test executable for queues
add_executable(test_queues tests/test_queues.cpp)
target_link_libraries(test_queues
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME QueueTests COMMAND test_queues)

# Test executable for orderbook
add_executable(test_orderbook tests/test_orderbook.cpp)
target_link_libraries(test_orderbook
    micromatch_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME OrderBookTests COMMAND test_orderbook)

# Test executable for matching engine
add_executable(test_matching_engine tests/test_matching_engine.cpp)
target_link_libraries(test_matching_engine
    micromatch_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
add_test(NAME MatchingEngineTests COMMAND test_matching_engine)

# Benchmark executable (if Google Benchmark is found)
if(benchmark_FOUND)
    add_executable(benchmark_queues tests/benchmark_queues.cpp)
    target_link_libraries(benchmark_queues
        benchmark::benchmark
        benchmark::benchmark_main
        Threads::Threads
    )
    message(STATUS "Google Benchmark found - building benchmarks")
else()
    message(STATUS "Google Benchmark not found - skipping benchmarks")
endif()

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler flags (Release): ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "Compiler flags (Debug): ${CMAKE_CXX_FLAGS_DEBUG}")
