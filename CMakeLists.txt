# Specify the minimum CMake version required to build the project.
cmake_minimum_required(VERSION 3.20)

# Define the project name and language.
project(ticker_fast CXX)

# Enforce the C++20 standard across all targets.
# Standard required prevents silently falling back to older C++ standards.
# Compiler extensions are disabled so we write standard, portable C++.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate operating system thread support library (pthread on Linux).
find_package(Threads REQUIRED)

# Default to Release build type if none was specified on the command line.
# Low-latency engines rely heavily on compiler optimizations (e.g. -O3).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Generate compile_commands.json, which LSP tools (like clangd) use to index the codebase.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizer configuration: TICKER_FAST_SANITIZER cache string variable.
# 'address' (ASan) and 'undefined' (UBSan) are reserved for Phase 11 hardening
# and are not implemented yet.
set(TICKER_FAST_SANITIZER "" CACHE STRING "Sanitizer to enable: 'thread', or empty (disabled).")

# Define an INTERFACE library for compiler warning flags.
# Using an INTERFACE target allows our targets to opt in without imposing these
# strict flags on third-party dependencies such as GoogleTest.
add_library(ticker_fast_warnings INTERFACE)
target_compile_options(ticker_fast_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# Apply ThreadSanitizer flags only to our own targets when requested.
if(TICKER_FAST_SANITIZER STREQUAL "thread")
    message(STATUS "Enabling ThreadSanitizer for project targets (-fsanitize=thread -g -O1)")
    target_compile_options(ticker_fast_warnings INTERFACE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(ticker_fast_warnings INTERFACE
        -fsanitize=thread
    )
endif()

# Define the core library as a STATIC library.
# Only setup code (shared memory) is compiled here. The ring buffer and protocol stay
# header-only so the hot path can be inlined into whatever uses it.
# shm_open/shm_unlink live in libc since glibc 2.34, so no extra 'rt' link is needed.
add_library(ticker_fast_core STATIC
    src/shm_segment.cpp
)
target_include_directories(ticker_fast_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# PRIVATE: our own source gets the strict warnings (and sanitizer flags) without
# forcing them on whoever links the library.
target_link_libraries(ticker_fast_core PRIVATE
    ticker_fast_warnings
)

# Download and integrate GoogleTest v1.14.0 via FetchContent at configure time.
# We mark the dependency as SYSTEM so third-party header warnings do not trigger
# our strict warning flags.
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    SYSTEM
)
# Prevent GoogleTest from overriding compiler runtime settings on Windows if built there.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable CTest infrastructure and recurse into the tests directory.
enable_testing()
add_subdirectory(tests)

# Throughput benchmark for the Phase 4 ring buffer toggles.
# Only Release numbers mean anything; Debug builds it too, just to catch compile errors.
add_executable(mechanical_sympathy_bench benchmarks/mechanical_sympathy_bench.cpp)
target_link_libraries(mechanical_sympathy_bench PRIVATE
    ticker_fast_core
    ticker_fast_warnings
    Threads::Threads
)
